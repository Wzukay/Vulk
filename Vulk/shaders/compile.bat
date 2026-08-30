@echo off
setlocal enabledelayedexpansion
echo Compiling all shaders...

:: Loop through all .vert, .frag, and .comp files
for %%f in (*.vert *.frag *.comp) do (
    :: Grab the full filename (e.g., grass.vert)
    set "filename=%%f"
    
    :: Replace the dot with an underscore and append .spv (e.g., grass_vert.spv)
    set "outfile=!filename:.=_!.spv"
    
    echo Compiling %%f -^> !outfile!
    glslc "%%f" -o "!outfile!"
)

echo.
echo All shaders compiled successfully!
pause