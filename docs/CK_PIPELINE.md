# **Corridor Key Pipeline: Concept, Architecture, and Predictive Unmixing Workflow**

## **📋 TL;DR (Executive Summary)**

**Corridor Key** revolutionizes the classic chroma keying process by replacing pure color-difference algorithms with an **Artificial Intelligence (Convolutional Neural Networks)** model trained on the actual physics of light unmixing (**Predictive Unmixing**).

1. **The Traditional Problem:** Classic chroma keyers operate pixel by pixel in isolation. Areas with motion blur, fine hair, or transparencies contain mixed pixels (foreground \+ background). Mathematically separating them using only the 3 recorded color channels (RGB) to find 7 unknowns (Subject RGB \+ Background RGB \+ Transparency) is an underdetermined problem that is impossible to solve perfectly without spatial context.  
2. **The Solution:** The pipeline uses the original image (ideally linear) combined with an **Alpha Hint** (a rough mask provided by the user/auto-roto). The AI engine uses this *Hint* as a spatial context map to predict how light would behave if the green screen background had never been there.  
3. **Clean Outputs:** The model generates two crucial channels for the compositor: an ultra-refined **Alpha channel** and the **Unmixed Foreground in Straight Color (Unpremultiplied)**. This means that blurred or transparent edges have their original colors mathematically reconstructed at 100% theoretical opacity, natively eliminating green spill without destroying color fidelity.  
4. **The OFX Plugin (Runtime):** Acts as the high-performance bridge (written in C++), interfacing between the editing ecosystem (DaVinci Resolve/Fusion) and the AI inference engine. It utilizes shared memory and hardware acceleration (TensorRT/DirectML) to make practical use in production environments viable.

## **1\. The Fundamental Problem of Traditional Chroma Keying**

In traditional digital compositing, green screen keying relies on pure mathematical operations over color channels. Legacy software attempts to isolate a specific color (green or blue) and create a mask based on the tonal distance of each pixel relative to the key color.

However, when we look at the physics of image capture, we face the **pixel mixing problem**. This occurs mainly in regions containing:

* **Motion Blur:** When an object moves rapidly, the camera sensor integrates both the light from the object and the light from the green screen into a single pixel during the shutter's open time.  
* **Fine Hair and Fibers:** Elements that are smaller than the sampling area of a single sensor photosite create an unavoidable optical average.  
* **Transparencies and Reflections:** Glass, plastic, or metallic surfaces reflect or refract the green screen light directly into the lens.

Mathematically, the sensor provides us with only **3 known values (R, G, B)** per pixel. To achieve a perfect physical unmixing composition (recovering the object's true color to insert it into a new background), we would need to solve **7 unknowns** for every edge pixel:

* The true color of the foreground ($R_{fg}, G_{fg}, B_{fg}$)
* The true color of the original background ($R_{bg}, G_{bg}, B_{bg}$)
* The opacity/transparency factor ($\alpha$)

Attempting to deduce 7 independent variables from just 3 measured values is a mathematically ill-posed (underdetermined) problem. This is why classic keyers rely on massive manual intervention, creating *core mattes* (to keep the center opaque), *edge mattes* (to refine the border), and aggressive *despill* algorithms that frequently distort the subject's original edge colors, leaving them harsh or grayish.

## **2\. The Corridor Key Philosophy: Inspired by Sodium Vapor**

The core concept of Corridor Key is based on **Predictive Unmixing**. It revives the optical philosophy of the legendary *Sodium Vapor Matting* process developed in the 1950s (famous for its use in Disney's *Mary Poppins*).

In the Sodium Vapor process, a special camera captured two film strips simultaneously through a beam splitter prism:

1. One strip recorded the actor in normal colors, but chemically blocked the pure yellow light emitted by the background lit by sodium lamps (589 nm).  
2. The other strip, sensitive *only* to this 589 nm emission line, recorded a perfect photographic black-and-white silhouette.

The direct result of this optical process was a foreground element on a completely black background (*Straight/Unmixed Foreground*) and a precise retention map (*Holdout Matte*). During optical compositing, the foreground was simply **added** linearly over the new background, perfectly preserving natural anti-aliasing and transparencies without any color spill.

Corridor Key replicates this analog behavior using Convolutional Neural Networks (CNNs). Instead of trying to "erase" the green by creating a brute-force mathematical mask, the artificial intelligence learned to **predict the unmixed image of the actor on a pure black background**, keeping the true chromatic values of the edges intact, solving the 7 unknowns through context-guided machine learning.

## **3\. The Processing Pipeline (Dataflow and Architecture)**

Corridor Key's processing follows a specific data flow that fully leverages the image's spatial context to make semantic decisions.

### **Main Pipeline Components:**

1. **Input Frame (Linear RGB):** The original frame from the timeline. The AI engine performs physically much better when data is fed in linear color space (like EXR 16-bit half-float). Linear light perfectly obeys mathematical rules of addition and multiplication, allowing the AI to deduce actual light mixtures without the distortion introduced by gamma curves or logarithmic profiles.  
2. **Alpha Hint (Context Guide):** Neural networks can struggle to guess artistic intent (e.g., is the actor wearing a green shirt, or are there tracking markers on the green screen?). To bypass this, the pipeline requires an *Alpha Hint*. The user generates a quick, rough mask (via a basic 3D keyer, quick roto, or Magic Mask). This mask doesn't need perfect edges; it serves to provide a spatial probability map for the AI: *"Pixels in the white region belong to the subject; pixels in the black region belong to the background"*.  
3. **Spatial Neural Inference Engine:** The heart of the system. A deep neural network based on spatial convolutions analyzes the RGB frame in tandem with the *Alpha Hint*. Based on its training, the AI develops a "semantic understanding" of materials (human skin, fabric, hair strands, plastic). If the AI detects a grayish-green pixel on the edge of an arm with motion blur, it examines the inner pixels of the adjacent arm and uses that context to predict the original skin color at that specific point, actively rewriting the edge's color.  
4. **Dual Coordinated Outputs:**  
   * **Predicted Alpha Channel:** A refined transparency mask (alpha channel) that smoothly maps sub-pixel edges, hair, and motion blur.  
   * **Unmixed Foreground (Straight/Unpremultiplied Color):** The major technological breakthrough. The AI reconstructs the subject's image by discarding transparency at the edges (solid colors are "stretched" over the blur zones) with the green background completely and natively subtracted (intrinsic despill). When this *Straight Color* is multiplied by the *Predicted Alpha* during compositing in Fusion/Nuke, it results in an optically perfectly integrated element.

## **4\. Context Flow Diagrams**

To illustrate the information traffic and the coupling of the plugin to the AI engine.

### **General Image Pipeline Flowchart**

graph TD  
    A\[Timeline / Raw Footage\] \--\> B\[Conversion to Linear RGB Space\]  
    B \--\> C\[Rough Alpha Hint Generation\]  
      
    B \--\> D\[Corridor Key Inference Engine\]  
    C \--\> D\[Corridor Key Inference Engine\]  
      
    subgraph AI \[Neural Processing \- Predictive Unmixing\]  
        D \--\> E\[Spatial Context Analysis\]  
        E \--\> F\[Sub-Pixel Transparency Calculation\]  
        E \--\> G\[Edge Chromatic Reconstruction \- Native Despill\]  
    end  
      
    F \--\> H\[Output 1: Predicted Alpha\]  
    G \--\> I\[Output 2: Unmixed Foreground Straight Color\]  
      
    H \--\> J\[Final Comp Node Fusion/Nuke\]  
    I \--\> J  
    K\[New Background Plate\] \--\> J  
      
    J \--\> L\[Multiplication: Foreground × Alpha\]  
    L \--\> M\[Alpha Inversion for Background Holdout\]  
    M \--\> N\[Perfect Additive Over Operation\]

### **Data Sequence Diagram (Runtime & OFX Bridge)**

This diagram details the execution flow between the editing host (like DaVinci Resolve) and the inference hardware, managed by the OFX Plugin.

sequenceDiagram  
    autonumber  
    participant Host as DaVinci Resolve (Timeline/Fusion)  
    participant Plugin as OFX Plugin (CorridorKey-Runtime C++)  
    participant IPC as Shared Memory / Pipes  
    participant Engine as AI Inference Engine (TensorRT/DirectML)

    Host-\>\>Plugin: Sends Original RGB Frame (GPU VRAM)  
    Host-\>\>Plugin: Sends Alpha Hint (Rough Mask)  
      
    Note over Plugin: Resolution Optimization\<br/\>(Applies 2K x 2K Anamorphic Squeeze, if configured)  
      
    Plugin-\>\>IPC: Allocates textures and maps ultra-fast data bridge (Zero-Copy)  
    IPC-\>\>Engine: Feeds AI model with tensors (Image \+ Hint)  
      
    Note over Engine: Neural Convolution Execution\<br/\>No Temporal Awareness (Single Frame Evaluation)  
    Note over Engine: Applies Spatial Weights to Edges  
      
    Engine--\>\>IPC: Returns Output Tensors (Predicted Alpha \+ Straight FG)  
    IPC--\>\>Plugin: Reads mapped data back to Host GPU memory  
      
    Note over Plugin: Undoes Anamorphic Squeeze\<br/\>(If applicable, interpolating mask with original 4K)  
      
    Plugin--\>\>Host: Returns Ready Channels to Fusion Node Tree

## **5\. Synthetic Data Training and Augmentation Strategy**

Because Neural Networks are only as good as the data that feeds them, the dataset creation strategy was fundamental to the success of *Predictive Unmixing*.

### **The Need for Procedural Synthetic Data**

Training the model with real green screen footage is unviable, as it is impossible to manually extract (with traditional tools) the absolute mathematical truth (*ground truth*) from the edges of moving hair to teach the machine what is "correct".

The training solution relied on a **High-Fidelity Procedural Synthetic Dataset**:

* Around 400 short *shots* (approx. 48 frames each) were generated.  
* Using procedural scripts in **Blender** and **Houdini**, thousands of variations were created by randomizing: materials, complex 3D models (digital humans, abstract glass/plastic geometries, particle systems), virtual camera paths (generating real motion blur), and lighting.  
* Because it was generated in an internal 3D engine, it was possible to extract the "Absolute Truth" separated by perfect passes: *Pure Green Background*, *Actor on Black Background (Premultiplied)*, *Pure Non-Transparent Actor Colors (Straight Color)*, and *Native Alpha Channel with flawless anti-aliasing*.

### **Critical Techniques in the Training Loop:**

1. **On-the-fly Augmentation:** During active training, each generated frame underwent heavy real-time distortions before feeding the network. This included geometric *squish/stretch*, dramatic variations in contrast, hue shifts, and blur. Crucially, in 25% of the iterations, the flawless procedural green background was dynamically replaced with photos of **real, dirty green screens**, containing folds, shadows, tape, and tracking markers. This vaccinated the neural network against practical studio imperfections.  
2. **Multi-Space Evaluation (Loss via RGB \+ YUV):** The neural network's error calculation (its loss function) does not evaluate only the standard RGB image. The algorithm isolates the data in **YUV** space (where Y is Luminance and UV is Chrominance). By separating chrominance, the system applies a severe punitive multiplier for color detection errors on the fringes (focusing on green spill). This allows the AI to ignore luma variations generated by cast shadows on the green screen without ruining the mask's stability.  
3. **Spatial Weighting Maps:** If an AI model tries to predict pure background areas (where the alpha should be 0), it can "explode," since any predicted color there will ultimately be multiplied by 0, leading the AI to believe any random guess is correct. To contain these background hallucinations and focus on edge complexity, the training included an internal spatial map: the exact *Alpha* edge regions were dilated by about 10 pixels. Any error the AI made **inside** this narrow edge band had its penalty heavily multiplied (10x to 20x). Errors in the vast solid background areas had their weight set to zero, guiding the neurons to dedicate almost all their computational capacity solely to the actor's bounding fringes.

## **6\. The OFX Layer / Runtime Engineering**

A monumental challenge with Deep Learning pipelines (which usually operate in heavy PyTorch-based Python environments) is making them usable, fast, and integrated into C++ based commercial post-production software.

The development of the **CorridorKey-Runtime / OFX Plugin** package acts as a critical engineering bridge:

* **Optimized Inter-Process Communication (IPC):** The plugin (written in C++) establishes ultra-low latency communication channels with the AI server process operating in the *background*. It moves heavy linear image buffers directly from the host GPU (DaVinci/Fusion) to the AI Engine using Shared Memory and *Named Pipes*, minimizing costly CPU roundtrips.  
* **Hardware-Agnostic Acceleration:** Strategic use of directly compiled inference libraries (such as **NVIDIA TensorRT**, **DirectML** for generalist Windows GPUs, and **Apple Metal/CoreML**). This substantially reduces raw VRAM usage (making the process viable on standard consumer GPUs with 4GB to 8GB) compared to running raw weight models in PyTorch.  
* **Anamorphic Resolution Trick (Scale Optimization):** To maintain timely asynchronous processing within the user interface for 4K media or higher, the technical implementation (as in the initial version) employs an *anamorphic squeeze*. The AI model prefers to evaluate matrices around 2K x 2K. The plugin squeezes the frame's width to this fixed aspect ratio before requesting AI inference. When the Alpha and Unmixed Foreground results return, they are "stretched" back to the native composition resolution. Due to the diffuse nature of fringe processing, this anamorphic sampling alteration on the edge is optically undetectable in most real-world productions, keeping the high-frequency (opaque) details untouched.

## **7\. Next Steps and Evolution Opportunities (Baseline for Improvements)**

With the base architecture solidified and validated, the open repository focuses on the following development and refactoring horizons for the community (Corridor Key 2.0 and beyond):

1. **Implementation of Temporal Awareness (Temporal Recurrence):**  
   * *The Challenge:* Currently, the model operates in a single-frame vacuum (strictly spatial). This generates subtle, continuous flickering on sub-pixel hair details between moving frames, as the network "guesses" the slightly different light mixture at every instant.  
   * *Improvement:* Introduce historical data from the previous tensor into the layers (architectures similar to ConvLSTM or Temporal Attention Transformers) so that Frame N processing is referenced by motion flow vectors from Frame N-1, creating solid temporal locking on the edges.  
2. **Full Native HDR (High Dynamic Range) Support and Training:**  
   * *The Challenge:* The pipeline works comfortably in the standard floating-point range (SDR with linear headroom), but convolutional neural network operations can "clip" luminous highlights that massively exceed the SDR limits of EXR media (blown-out *specular highlights*).  
   * *Improvement:* Render massive new packs of synthetic *Ground Truth* data containing extreme, unclipped HDR intensities in translucent glass/lens reflections, refining the model to preserve the complete dynamic range.  
3. **Studio Shadow Extraction (Bounce Light / Shadow Pass Isolation):**  
   * *The Challenge:* The current integrated despill cancels green reflections and might subtract the darkening generated by the actor's feet shadows on the green screen floor as if it were generic opacity.  
   * *Improvement:* Redesign the final branches of the model's output to deliver a distinct third channel focused purely on isolating cast shadows. This will allow compositors to not only unmix the actor but to actively inject the actor's real shadows (via Multiply/Plus) perfectly remapped over the final CG set geometry.  
4. **Dynamic Native Resolution vs. Tiling:**  
   * *The Challenge:* The current fixed anamorphic method hits hard limits in the 6K/8K era.  
   * *Improvement:* Optimize the OFX IPC bridge to intelligently slice problematic pixels (*Tile Processing*), where the network inference focuses heavy processing resources only on windows containing the boundary pixels of uncertainty from the original Alpha Hint, maintaining full resolution without anamorphic down-sampling.
