# Openaslc

[![CI](https://github.com/sourabhbodas/openaslc/actions/workflows/ci.yml/badge.svg)](https://github.com/sourabhbodas/openaslc/actions/workflows/ci.yml)

**Open Source Agentic Software Logic Controller** designed to run efficiently on any single-board SOC (System on Chip).

---

## License & Commercial Notice

**openaslc** is a **source-available** project distributed under the terms of the **PolyForm Noncommercial License 1.0.0**. 

* **Free for Education & Research:** Students, educators, academic researchers, and hobbyists are free to use, modify, and experiment with this codebase.
* **Commercial Use Restricted:** Any production deployment, inclusion in commercial products, industrial automation use by for-profit companies, or monetization of this software is strictly prohibited under this license.

### Commercial Licensing
If your company wants to deploy `openaslc` in a production environment, build commercial hardware around it, or obtain a custom commercial license, please contact the maintainer directly.

---

## Getting Started

### Prerequisites
To build and deploy `openaslc`, you will need:
* A supported single-board SOC (e.g., Raspberry Pi, BeagleBone, etc.)
* A C++ compiler supporting modern C++ standards
* **CMake** (v3.10 or higher)

### Build & Installation

Clone the repository along with its submodules:

```bash
git clone https://github.com
cd openaslc
```

Build the project using CMake:

```bash
mkdir build && cd build
cmake ..
make
```

### Running Tests

To verify the installation, execute the test suite:

```bash
# Inside the build directory
ctest
```

---

## Repository Structure

* `src/`: Core implementation logic for the Agentic Software Logic Controller.
* `include/openaslc/`: Public headers and API definitions.
* `tests/`: Automated unit and integration tests.
* `CMakeLists.txt`: Build configuration file.

---

## Contributing

We welcome contributions from students, researchers, and hobbyists! 

Please note that by contributing to `openaslc`, you agree that your code contributions will be licensed under the same PolyForm Noncommercial License 1.0.0.

1. Fork the project.
2. Create your feature branch (`git checkout -b feature/AmazingFeature`).
3. Commit your changes (`git commit -m 'Add some AmazingFeature'`).
4. Push to the branch (`git push origin feature/AmazingFeature`).
5. Open a Pull Request.

