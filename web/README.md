# AntChain Workbench

A local Next.js interface for running the dependency-free C++ AntChain experiment and inspecting the generated condition summary.

## Prerequisites

- Node.js 18.18 or newer
- A C++17 toolchain and [CMake](https://cmake.org/download/), both available on `PATH`

On Windows, the Visual Studio Build Tools with the **Desktop development with C++** workload are a good default. The first experiment run configures and builds `../cpp`; later runs reuse that executable.

## Run it

```powershell
cd web
npm install
npm run dev
```

Open `http://localhost:3000`. Select the miner population, block count, TSP size, strategic share, seed, and hybrid λ values, then choose **Run C++ experiment**.

The browser sends only bounded numeric parameters to the local Next.js route. That route creates a new `web/results/run-*` directory, invokes the compiled C++ simulator with those values, and returns its `cpp_summary.csv` rows to the workbench. Run artifacts are deliberately ignored by Git.

## Checks

```powershell
npm run build
```

The existing static explainer remains available as `explainer.html`; the Next.js workbench is the runnable application.
