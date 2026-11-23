# Direct3D 12 PBR Renderer

A real-time Direct3D 12 renderer built with C++ and HLSL, featuring physically based rendering (PBR) materials, shadow mapping, and a freely movable camera.

RasterLab loads OBJ models and uses base color, roughness, metallic, and normal maps to describe how their surfaces respond to light. Models cast shadows onto other objects in the scene, and keyboard-and-mouse controls let you inspect the materials and lighting from different angles.

I built this project to learn how a renderer works directly with the graphics API: creating GPU resources, uploading models and textures, writing shaders, managing resource transitions, and recording draw commands.

## Preview

**Screenshot**

<img width="953" height="927" alt="image" src="https://github.com/user-attachments/assets/7436405d-87b6-4e5f-9fcd-1cde62014708" />


**Video demo**

https://github.com/user-attachments/assets/3ba2e43c-9802-40e6-b6db-b778d540fec2



## Features

- OBJ model loading and object transformations.
- Perspective projection and depth testing.
- PBR materials using base color, roughness, metallic, and normal texture maps.
- GGX specular highlights, Schlick Fresnel, and Lambertian diffuse lighting.
- Shadow mapping with percentage-closer filtering (PCF) to soften shadow edges.
- A light blue sky gradient.
- A movable camera with keyboard and mouse controls.

## How it works

### PBR materials and textures

The renderer uses a metallic-roughness PBR workflow. Instead of giving an entire model one uniform appearance, texture maps control its material properties across the surface. The model's UV coordinates determine which part of each texture is sampled for each pixel.

| Texture map | What it controls |
| --- | --- |
| Base color | The surface color, including details such as paint, labels, and wear. For metals, it also determines the color of specular reflections. |
| Roughness | How concentrated or spread out the specular highlights appear. Lower values create sharper highlights; higher values create broader highlights. |
| Metallic | Whether a surface behaves like a non-metal or a metal. This changes the balance between diffuse and specular lighting. |
| Normal | Small surface details that change the lighting direction without adding more geometry, such as bumps, scratches, and dents. |

The pixel shader combines these maps with the light direction and viewing direction. It uses a GGX distribution for specular highlights, Schlick Fresnel for the change in reflectivity with viewing angle, and a Lambertian diffuse term. Base color textures are read as sRGB, while roughness, metallic, and normal maps are treated as linear data.

The current lighting comes from a directional light. Environment lighting and image-based reflections are possible future additions.

### Shadow mapping

Shadows are rendered in two passes:

1. **Shadow pass:** The scene is drawn from the light's viewpoint into a depth texture. This shadow map stores the depth of the closest surface seen by the light.
2. **Camera pass:** The scene is drawn from the camera's viewpoint. Each surface point is projected into the shadow map and its depth is compared with the stored depth. A surface behind the recorded depth is blocked from the light and receives less lighting.

Percentage-closer filtering (PCF) samples several nearby locations in the shadow map and averages their depth-comparison results. This softens the shadow edges. A small depth bias and a correction for surface slope help reduce false self-shadowing, especially when filtering across a flat or sloped surface.

### Camera and scene

The camera updates once per frame using keyboard input, relative mouse movement, and elapsed time. Its position and viewing directions are sent to the shaders so the scene can be viewed from different angles. Perspective projection gives objects a sense of distance, while depth testing determines which surfaces are visible. A sky gradient is drawn behind the scene.

## Build and run

1. Use Windows with a Direct3D 12-capable GPU.
2. Open `Direct3D Rasterizer.slnx` in Visual Studio with the **Desktop development with C++** workload and a Windows SDK installed. The project currently uses the **v145** C++ toolset.
3. Restore the NuGet packages to install DirectX Tool Kit for DirectX 12.
4. Select **Debug | x64**, then build and run.

Set the debugger's working directory to `$(ProjectDir)` so the application can find its shaders, models, and textures. When launching the executable manually, use the inner `Direct3D Rasterizer` folder as the working directory.

The default scene uses `small_lpg_tank_4k.obj`, `plane.obj`, and the tank's four PNG texture maps. Their filenames are set in `main.cpp`.

## Camera controls

| Input | Action |
| --- | --- |
| W / S | Move forward / backward |
| A / D | Move left / right |
| Q / E | Move down / up |
| Hold right mouse button and move | Look around |
| Shift | Move faster |
| R | Reset the camera |
| Escape | Release mouse look |

## Code layout

- `main.cpp` — Direct3D setup, asset loading, materials, and render passes.
- `Camera.h` / `Camera.cpp` — camera movement and input handling.
- `shader.hlsl` — object transformations, projection, lighting, texture sampling, shadows, and sky rendering.

This is a learning project that grows as I explore more rendering techniques.
