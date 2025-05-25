# FBX Processor

A C++ application that processes FBX files according to the following requirements:
- Splits each FBX file into multiple individual files, one for each skeleton
- Names each new file after the original file, appending the actor's name as a suffix
- Moves each actor to the center of the stage while preserving all motions (Y-axis unchanged)
- Optionally rotates each actor to face the positive Z direction

## Requirements

- C++17 compatible compiler
- CMake 3.10 or higher
- Autodesk FBX SDK (tested with version 2020.3.1)

## Building the Project

1. Clone or download this repository
2. Edit the `CMakeLists.txt` file to set the `FBX_SDK_ROOT` to your FBX SDK installation path
3. Create a build directory and navigate to it:
   ```
   mkdir build
   cd build
   ```
4. Run CMake configuration:
   ```
   cmake ..
   ```
5. Build the project:
   ```
   cmake --build .
   ```
### WARNING: 
In some cases, the libfbxsdk.dylib file that is located in the FBX directory is not being propeperly located by the build. 
In that scenario, you can directly copy the file into the build folder, or specify it's location directly in the CMakeLists.txt in atttempt to solve this.

## Usage

Run the program with the following command:

```
FBXProcessor <directory_path> [rotate_to_face_z]
```

Parameters:
- `directory_path`: Path to the directory containing FBX files to process
- `rotate_to_face_z`: Optional flag (0 or 1) to rotate actors to face Z direction (defaults to 0)

Example:
```
FBXProcessor C:/mocap_data 1
```

This will process all FBX files in the data directory and rotate actors to face the positive Z direction.

## How It Works

1. The application recursively processes all FBX files in the specified directory.
2. For each file, it:
   - Identifies all skeleton root nodes in the file
   - Creates a new FBX file for each skeleton, copying all animation data
   - Centers the actor by modifying the root translation while preserving Y-axis position
   - Optionally rotates the actor to face the positive Z direction
   - Saves the result as a new file with the naming convention `[original_name]_[actor_name].fbx`

## Notes

- The application requires the Autodesk FBX SDK to compile and run
- Actor names are derived from node names in the FBX
