# How To Collect Traces

The `CollectTraces` class provides an RAII helper to collect OpenUSD performance traces during test execution.

## Usage

1. Include the header and create a `CollectTraces` instance at the beginning of your test:

```cpp
#include <RenderingFramework/CollectTraces.h>

TEST(MyTest, SomeTest)
{
    RenderingUtils::CollectTraces traces;

    // ... your test code ...
    // Tracing happens automatically while 'traces' is in scope
}
```

2. Run your test with the `PXR_ENABLE_GLOBAL_TRACE` environment variable set:

```bash
PXR_ENABLE_GLOBAL_TRACE=1 ./your_test_executable
```

3. After the test completes, a `report.json` file will be generated in the current working directory.

## Viewing the Trace Report

The generated `report.json` file is in Chrome Tracing format. You can visualize it using:

### Option 1: Chrome Tracing (built-in)

1. Open Google Chrome
2. Navigate to `chrome://tracing`
3. Click the **Load** button in the top-left corner
4. Select your `report.json` file

### Option 2: Perfetto UI (recommended)

1. Open https://ui.perfetto.dev/ in your browser
2. Drag and drop the `report.json` file onto the page

## Interpreting the Results

The trace viewer displays an interactive timeline showing:

- **Function call hierarchy**: Nested blocks show parent-child relationships
- **Timing information**: Duration of each traced scope
- **Thread activity**: Events grouped by thread

Use the mouse to:
- **Scroll**: Zoom in/out on the timeline
- **Click + drag**: Pan across the timeline
- **Click on a block**: View detailed timing information

## Adding Custom Trace Points

To add tracing to your own code, use the OpenUSD TRACE macros:

```cpp
#include <pxr/base/trace/trace.h>

void MyFunction()
{
    TRACE_FUNCTION();  // Traces the entire function

    {
        TRACE_SCOPE("CustomScopeName");  // Traces a specific block
        // ... code to trace ...
    }
}
```
