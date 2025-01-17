# PDBToJSON

PDBToJSON is a command-line tool that parses Microsoft Program Database (PDB) files using the Debug Interface Access (DIA) SDK and extracts detailed symbol information. The extracted data includes classes, structures, enums, functions, global variables, and typedefs, which are then exported into a structured JSON file.

## Features

- **PDB Parsing**: Reads PDB files to extract comprehensive symbol information.
- **JSON Output**: Exports the symbol data to a human-readable `pdb_dump.json` file.
- **Symbol Filtering**: Allows filtering of symbols based on a file prefix.
- **Progress Display**: Shows the processing progress in the console window title with one decimal precision.
- **Type Name Caching**: Implements caching of type names to optimize performance.
- **Handles Basic Types**: Correctly resolves basic types like `int`, `char`, etc.
- **Optimized Mutex Usage**: Avoids deadlocks by careful mutex handling without the need for recursive mutexes.

## Requirements

- **Operating System**: Windows (due to the use of the DIA SDK and Windows-specific APIs).
- **Compiler**: Microsoft Visual C++ (recommended for compatibility with the DIA SDK).
- **DIA SDK**: Must be installed and accessible to the project. Typically included with Visual Studio or the Windows SDK.
- **Dependencies**:
  - **nlohmann/json**: A single-header JSON library for C++. Download from [GitHub](https://github.com/nlohmann/json) and include it in your project.
  - **ATL Libraries**: For `CComPtr`. Ensure that ATL is installed and configured in your development environment.

## Setup and Build Instructions

### 1. Install Dependencies

- **DIA SDK**:
  - Ensure that the DIA SDK is installed. It is typically included with Visual Studio or the Windows SDK.
  - If not installed, download and install the latest Windows SDK from the [Microsoft website](https://developer.microsoft.com/en-us/windows/downloads/windows-10-sdk/).

- **nlohmann/json**:
  - Download the single-header `json.hpp` file from the [GitHub repository](https://github.com/nlohmann/json/releases).
  - Place the `json.hpp` file in your project's include directory.

### 2. Clone or Download the Repository

Clone the repository or download the source files to your local machine.

```sh
git clone https://github.com/emcifuntik/pdb-to-json.git
```

### 3. Open the Project in Visual Studio

- Open Visual Studio.
- Create a new **Console Application** project.
- Add the provided `main.cpp` source file to the project.

### 4. Configure Project Properties

- **Include Directories**:
  - Add the path to the DIA SDK headers to your project's include directories.
    - Typically located at `C:\Program Files\Microsoft Visual Studio\2022\Enterprise\DIA SDK\include` (adjust according to your installation).
  - Add the path to the directory containing `json.hpp`.

- **Library Directories**:
  - Add the path to the DIA SDK library files to your project's library directories.
    - Typically located at `C:\Program Files\Microsoft Visual Studio\2022\Enterprise\DIA SDK\lib`.

- **Additional Dependencies**:
  - Ensure that `diaguids.lib` is included in the linker dependencies.
    - Go to **Project Properties** > **Linker** > **Input** > **Additional Dependencies** and add `diaguids.lib`.

- **Character Set**:
  - Set the project to use **Unicode Character Set**.
    - Go to **Project Properties** > **Advanced** > **Character Set** and select **Use Unicode Character Set**.

### 5. Build the Project

- Build the solution (usually `Ctrl+Shift+B` in Visual Studio).
- Resolve any build errors by verifying include paths and dependencies.

## Usage

Open a command prompt and navigate to the directory containing the compiled `PDBToJSON.exe` executable.

```sh
PDBToJSON.exe <path-to-pdb-file> [file-prefix]
```

- **`<path-to-pdb-file>`**: The full path to the PDB file you want to analyze.
- **`[file-prefix]`** (optional): A prefix string to filter symbols based on their source file paths. Only symbols defined in files starting with this prefix will be processed.

### Examples

- **Process all symbols in a PDB file**:

  ```sh
  PDBToJSON.exe "C:\Symbols\myprogram.pdb"
  ```

- **Process symbols only from files in the `src` directory**:

  ```sh
  PDBToJSON.exe "C:\Symbols\myprogram.pdb" "C:\Projects\MyProgram\src\"
  ```

## Output

- The program outputs a file named `pdb_dump.json` in the current directory.
- The JSON file contains the following top-level keys:
  - **`Classes`**: An array of class and structure definitions.
  - **`Enums`**: An array of enumerations.
  - **`GlobalFunctions`**: An array of global function definitions.
  - **`GlobalVariables`**: An array of global variable definitions.
  - **`Typedefs`**: An array of type definitions.

### JSON Structure

Below is an example of the JSON structure for a class:

```json
{
  "Name": "MyClass",
  "Size": 32,
  "SourceFile": "C:\\Projects\\MyProgram\\src\\MyClass.cpp",
  "LineNumber": 10,
  "BaseClasses": [
    {
      "Name": "BaseClass",
      "IsVirtual": false,
      "Offset": 0
    }
  ],
  "Fields": [
    {
      "Name": "memberVariable",
      "Type": "int",
      "IsStatic": false,
      "IsConst": false,
      "Offset": 4,
      "VirtualOffset": 0
    }
  ],
  "Methods": [
    {
      "Name": "MyMethod",
      "IsVirtual": true,
      "IsPureVirtual": false,
      "IsStatic": false,
      "IsConst": false,
      "VirtualMethodIndex": 0,
      "VirtualOffset": 0,
      "Parameters": [
        {
          "Type": "int"
        }
      ]
    }
  ]
}
```

## Notes

- **Progress Display**: While processing, the console window title updates to show the progress percentage with one decimal precision.
- **Symbol Filtering**: The file prefix filter is useful when you only want to process symbols defined in your project's source files.
- **Type Resolution**: The program resolves basic types and handles pointers and arrays.
- **Caching**: Type names are cached to improve performance during processing.
- **Mutex Handling**: Careful mutex management avoids deadlocks during recursive type name resolution.

## Troubleshooting

- **Missing DIA SDK**: If you receive errors about missing DIA SDK components, ensure that the SDK is installed and that include and library paths are correctly configured.
- **Permission Errors**: Make sure you have read permissions for the PDB file you are trying to process.
- **Unsupported PDB Formats**: The tool works with PDB files compatible with the DIA SDK. Older or corrupted PDB files may not be supported.

## License

This project is licensed under the MIT License. See the [LICENSE](LICENSE) file for details.
