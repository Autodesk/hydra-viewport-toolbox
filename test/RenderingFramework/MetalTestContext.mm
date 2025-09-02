//
// Copyright 2023 by Autodesk, Inc.  All rights reserved.
//
// This computer source code and related instructions and comments
// are the unpublished confidential and proprietary information of
// Autodesk, Inc. and are protected under applicable copyright and
// trade secret law.  They may not be disclosed to, copied or used
// by any third party without the prior written consent of Autodesk, Inc.
//

#include <RenderingFramework/MetalTestContext.h>

#import <UIKit/UIKit.h>
#import <Metal/Metal.h>
#import <MetalKit/MetalKit.h>
#import <QuartzCore/QuartzCore.h>

#include <RenderingFramework/pxrusd.h>
PXR_USD_IMPORT_BEGIN
#include <pxr/imaging/hgiMetal/hgi.h>
#include <pxr/imaging/hgiMetal/texture.h>
#include <pxr/imaging/hdx/types.h>
#include <pxr/imaging/hgi/blitCmdsOps.h>
PXR_USD_IMPORT_END

#include <hvt/engine/framePass.h>
#include <hvt/engine/hgiInstance.h>

#include <RenderingUtils/stb/stb_image.h>
#include <RenderingUtils/stb/stb_image_write.h>

#include <filesystem>


/// Convenience helper functions for internal use in unit tests
namespace TestHelpers
{

id<CAMetalDrawable> _drawable;
id<MTLCommandBuffer> _commandBuffer;
id<MTLRenderCommandEncoder> _renderEncoder;
MTLRenderPassDescriptor* _renderPassDescriptor;

id<MTLDevice> _hgiMetalDevice;
id<MTLRenderPipelineState> _hgiMetalBlitToFramebuffer;
dispatch_semaphore_t _inFlightSemaphore;

constexpr uint32_t AAPLMaxBuffersInFlight = 3;

/// Prepares the Metal objects for copying to the framebuffer.
bool loadMetal()
{
    _hgiMetalDevice = MTLCreateSystemDefaultDevice();

    static const std::string vertexSource =
        "#include <metal_stdlib>\n"
        "using namespace metal;\n"
        "\n"
        "struct VertexOut\n"
        "{\n"
        "    float4 position [[ position ]];\n"
        "    float2 texcoord;\n"
        "};\n"
        "\n"
        "vertex VertexOut vertexBlit(uint vid [[vertex_id]])\n"
        "{\n"
        "    // These vertices map a triangle to cover a fullscreen quad.\n"
        "    const float4 vertices[] = {\n"
        "        float4(-1, -1, 1, 1), // bottom left\n"
        "        float4(3, -1, 1, 1),  // bottom right\n"
        "        float4(-1, 3, 1, 1),  // upper left\n"
        "    };\n"
        "\n"
        "    const float2 texcoords[] = {\n"
        "        float2(0.0, 0.0), // bottom left\n"
        "        float2(2.0, 0.0), // bottom right\n"
        "        float2(0.0, 2.0), // upper left\n"
        "    };\n"
        "\n"
        "    VertexOut out;\n"
        "    out.position = vertices[vid];\n"
        "    out.texcoord = texcoords[vid];\n"
        "    return out;\n"
    "}\n";

    static const std::string fragmentSourceNoDepth =
        "\n"
        "fragment half4 fragmentBlitLinear(VertexOut in [[stage_in]], texture2d<float> colorTex[[texture(0)]])\n"
        "{\n"
        "    constexpr sampler colorSampler = sampler(address::clamp_to_edge);\n"
        "    const float4 pixel = colorTex.sample(colorSampler, in.texcoord);\n"
        "\n"
        "    return half4(pixel);\n"
        "}\n";

    // Build the Metal shader code with (or without) depth processing.

    const std::string finalShader = vertexSource + fragmentSourceNoDepth;
    NSString* shaderSource = [NSString stringWithUTF8String:finalShader.c_str()];

    // Set up the vertex and fragment shader programs.

    NSError* error = nullptr;

    id<MTLLibrary> defaultLibrary = [_hgiMetalDevice newLibraryWithSource:shaderSource options:nil error:&error];
    if (defaultLibrary == nil)
    {
        NSLog(@"Error: failed to create Metal library: %@", error);
    }

    id<MTLFunction> vertexFunction = [defaultLibrary newFunctionWithName:@"vertexBlit"];
    id<MTLFunction> fragmentFunction = [defaultLibrary newFunctionWithName:@"fragmentBlitLinear"];

    // Set up the pipeline state object.

    MTLRenderPipelineDescriptor* pipelineStateDescriptor = [[MTLRenderPipelineDescriptor alloc] init];
    pipelineStateDescriptor.rasterSampleCount = 1;
    pipelineStateDescriptor.vertexFunction = vertexFunction;
    pipelineStateDescriptor.fragmentFunction = fragmentFunction;

    // Mention if there is a z-depth buffer.
    pipelineStateDescriptor.depthAttachmentPixelFormat = MTLPixelFormatInvalid;

    // Configure the color attachment for blending.

    MTLRenderPipelineColorAttachmentDescriptor* colorDescriptor = pipelineStateDescriptor.colorAttachments[0];
    colorDescriptor.pixelFormat = MTLPixelFormatBGRA8Unorm;
    colorDescriptor.blendingEnabled = YES;
    colorDescriptor.rgbBlendOperation = MTLBlendOperationAdd;
    colorDescriptor.alphaBlendOperation = MTLBlendOperationAdd;
    colorDescriptor.sourceRGBBlendFactor = MTLBlendFactorOne;
    colorDescriptor.sourceAlphaBlendFactor = MTLBlendFactorOne;
    colorDescriptor.destinationRGBBlendFactor = MTLBlendFactorOneMinusSourceAlpha;

    // As we blit the texture and save screen shot from CAMetalDrawable.texture,
    // using MTLBlendFactorOneMinusSourceAlpha is necessary for supportting multipe viewports and minimap
    // TODO: Evaluate to use MTLBlendFactorZero after Metal backend is fully ready
    colorDescriptor.destinationAlphaBlendFactor = MTLBlendFactorOneMinusSourceAlpha;

    error = nullptr;
    _hgiMetalBlitToFramebuffer = [_hgiMetalDevice newRenderPipelineStateWithDescriptor:pipelineStateDescriptor error:&error];
    if (!_hgiMetalBlitToFramebuffer)
    {
        NSLog(@"Failed to created pipeline state, error %@", error);
    }
    return (bool)_hgiMetalBlitToFramebuffer;
}

/// Compose the color texture to the framebuffer.
void composeToFrameBuffer(
    id<MTLCommandBuffer> commandBuffer,
    id<MTLTexture> renderPassColorTexture,
    const pxr::GfVec4d& renderPassViewport)
{
    MTLRenderPassDescriptor* renderPassDescriptor = _renderPassDescriptor;
    if (!renderPassDescriptor)
    {
        return;
    }

    renderPassDescriptor.colorAttachments[0].loadAction = MTLLoadActionLoad;

    // Set render pass dimensions.
    renderPassDescriptor.renderTargetWidth  = renderPassViewport[2];
    renderPassDescriptor.renderTargetHeight = renderPassViewport[3];

    // Create a render command encoder to encode copy command.
    id <MTLRenderCommandEncoder> renderEncoder = [commandBuffer renderCommandEncoderWithDescriptor:renderPassDescriptor];
    if (!renderEncoder)
    {
        return;
    }

    static bool loadMetalDone = false;
    if (!loadMetalDone)
    {
        loadMetalDone = loadMetal();
    }

    // Blit the texture to the view.
    [renderEncoder pushDebugGroup:@"FinalBlit"];
    [renderEncoder setFragmentTexture:renderPassColorTexture atIndex:0];
    [renderEncoder setRenderPipelineState:_hgiMetalBlitToFramebuffer];
    [renderEncoder drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
    [renderEncoder popDebugGroup];

    // Finish encoding the copy command.
    [renderEncoder endEncoding];
}

/// Displays a frame pass.
void MetalRendererContext::displayFramePass(hvt::FramePass* framePass)
{
    auto colorTexHandle = framePass->GetRenderTexture(pxr::HdAovTokens->color);
    id<MTLTexture> renderPassColorTexture = static_cast<pxr::HgiMetalTexture*>(colorTexHandle.Get())->GetTextureId();

    pxr::HgiMetal* hgi = static_cast<pxr::HgiMetal*>(_hgi.get());

    const pxr::GfVec4d renderPassViewport = { framePass->GetDisplayWindow().GetMin()[0],
        framePass->GetDisplayWindow().GetMin()[1],
        framePass->GetDisplayWindow().GetSize()[0],
        framePass->GetDisplayWindow().GetSize()[1] };

    if (!_inFlightSemaphore)
    {
        _inFlightSemaphore = dispatch_semaphore_create(AAPLMaxBuffersInFlight);
    }

    hgi->StartFrame();

    // Create a command buffer to blit the texture to the framewbuffer.
    id<MTLCommandBuffer> commandBuffer = hgi->GetPrimaryCommandBuffer();
    __block dispatch_semaphore_t blockSemaphore = _inFlightSemaphore;
    [commandBuffer addCompletedHandler:^(__unused id<MTLCommandBuffer> buffer) {
        dispatch_semaphore_signal(blockSemaphore);
    }];

    // Compose the rendered texture to the framebuffer.
    composeToFrameBuffer(commandBuffer, renderPassColorTexture, renderPassViewport);

    // Tell Hydra to commit the command buffer and complete the work.
    hgi->CommitPrimaryCommandBuffer();
    hgi->EndFrame();
}

MetalRendererContext::MetalRendererContext(int w, int h) : HydraRendererContext(w, h)
{
    init();
    createHGI(pxr::TfToken("Metal"));
}

MetalRendererContext::~MetalRendererContext()
{
    destroyHGI();
    shutdown();
}

void MetalRendererContext::init()
{
    SDL_SetHint(SDL_HINT_RENDER_DRIVER, "metal");

    _window = SDL_CreateWindow("Test Window", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, width(),
        height(), SDL_WINDOW_FULLSCREEN);

    if (!_window)
    {
        std::stringstream str;
        str << "Error creating window: " << SDL_GetPlatform() << ": " << SDL_GetError() << ".";
        throw std::runtime_error(str.str());
    }

    _renderer = SDL_CreateRenderer(_window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!_renderer)
    {
        std::stringstream str;
        str << "Error creating renderer: " << SDL_GetPlatform() << ": " << SDL_GetError() << ".";
        throw std::runtime_error(str.str());
    }

    SDL_SetHint(SDL_HINT_IME_SHOW_UI, "1");
}

void MetalRendererContext::shutdown()
{
    SDL_DestroyRenderer(_renderer);
    SDL_DestroyWindow(_window);
    SDL_Quit();
}

void MetalRendererContext::beginMetal()
{
    CAMetalLayer* layer = (__bridge CAMetalLayer*)SDL_RenderGetMetalLayer(_renderer);
    layer.pixelFormat = MTLPixelFormatBGRA8Unorm;

    id<MTLCommandQueue> commandQueue = [layer.device newCommandQueue];
    _renderPassDescriptor = [MTLRenderPassDescriptor new];

    // Setup the framebuffer.

    int width, height;
    SDL_GetRendererOutputSize(_renderer, &width, &height);
    layer.drawableSize = CGSizeMake(width, height);
    _drawable = [layer nextDrawable];
    _commandBuffer = [commandQueue commandBuffer];

    // Setup the framebuffer color texture.

    // Set the render pass dimension in case it changes.
    _renderPassDescriptor.renderTargetWidth  = width;
    _renderPassDescriptor.renderTargetHeight = height;

    _renderPassDescriptor.colorAttachments[0].clearColor  = MTLClearColorMake(0, 0, 0, 1);
    _renderPassDescriptor.colorAttachments[0].texture     = _drawable.texture;
    _renderPassDescriptor.colorAttachments[0].loadAction  = MTLLoadActionLoad;
    _renderPassDescriptor.colorAttachments[0].storeAction = MTLStoreActionStore;

    _renderEncoder = [_commandBuffer renderCommandEncoderWithDescriptor:_renderPassDescriptor];
    [_renderEncoder pushDebugGroup:@"AGPTest"];
}

void MetalRendererContext::endMetal()
{
    [_renderEncoder popDebugGroup];
    [_renderEncoder endEncoding];

    [_commandBuffer presentDrawable:_drawable];
    [_commandBuffer commit];
}

void MetalRendererContext::run(std::function<bool()> render, hvt::FramePass* framePass)
{
    bool moreFrames = true;
    while (moreFrames)
    {
        // To guarantee a correct cleanup even in case of errors.
        struct Guard
        {
            explicit Guard(MetalRendererContext* context) : _context(context) { _context->beginMetal(); }
            ~Guard() { _context->endMetal(); }
            MetalRendererContext* _context { nullptr };
        } guard(this);

        try
        {
            moreFrames = render();
            displayFramePass(framePass);
        }
        catch(const std::exception& ex)
        {
            throw std::runtime_error(std::string("Failed to render the frame pass: ") + ex.what() + ".");
        }
        catch(...)
        {
            throw std::runtime_error(std::string("Failed to render the frame pass: Unexpected error."));
        }
    }
}

bool MetalRendererContext::saveImage(const std::string& fileName)
{
    // Creates the root destination path if needed.

    const std::filesystem::path dirPath      = documentDirectoryPath() + "/Data";
    // The fileName could contain part of the directory path.
    const std::filesystem::path fullFilePath = getFilename(dirPath, fileName + "_computed");
    const std::filesystem::path directory    = fullFilePath.parent_path();

    NSFileManager* fileManager = [[NSFileManager alloc] init];
    NSString* dirPathToCreate = [NSString stringWithFormat:@"%s", directory.c_str()];
    BOOL isDirectory = TRUE;
    if (![fileManager fileExistsAtPath:dirPathToCreate isDirectory:&isDirectory])
    {
        NSError* error = nil;
        if (![fileManager createDirectoryAtPath:dirPathToCreate withIntermediateDirectories:YES attributes:nil error:&error])
        {
            throw std::runtime_error(std::string("Failed to create the directory: ") + [dirPathToCreate UTF8String]);
        }
    }

    // Removes existing file if it exists.

    NSString* screenShotPath = [NSString stringWithFormat:@"%s", fullFilePath.c_str()];
    [fileManager removeItemAtPath:screenShotPath error:nil];

    NSLog(@"Screen shot - %@", screenShotPath);

    // Saves the render result.

    CIImage* ciImage = [CIImage imageWithMTLTexture:_drawable.texture options:nil];
    ciImage = [ciImage imageByCroppingToRect:CGRectMake(0, 0, width(), height())];
    ciImage = [ciImage imageByApplyingCGOrientation:kCGImagePropertyOrientationDownMirrored];
    UIImage* uiImage = [UIImage imageWithCIImage:ciImage];
    NSData* data = UIImagePNGRepresentation(uiImage);
    return [data writeToFile:screenShotPath atomically:TRUE];
}

MetalTestContext::MetalTestContext()
{
    init();
}

MetalTestContext::MetalTestContext(int w, int h) : TestContext(w, h)
{
    init();
}

void MetalTestContext::init()
{
    _sceneFilepath = mainBundlePath() + "/data/assets/usd/test_fixed.usda";

    // Create the renderer context required for Hydra.
    _backend = std::make_shared<TestHelpers::MetalRendererContext>(_width, _height);
    if (!_backend)
    {
        throw std::runtime_error("Failed to initialize the unit test backend!");
    }
    
    _backend->setDataPath(mainBundlePath() + "/data/");
}

} // namespace TestHelpers
