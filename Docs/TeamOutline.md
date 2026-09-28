# The team outline

Allies are outlined in blue and enemies in red, as in Atlas Reactor. The code
already does its half: every unit's body writes a stencil value (1 for an ally,
2 for an enemy, from where the player stands), and the project has custom depth
with stencil switched on (`Config/DefaultEngine.ini`, `r.CustomDepth=3`). The
director adds `/Game/UI/M_TeamOutline` to the camera as a post-process when that
material exists, and hands it the colours. Until it exists, units have no
outline and the log says so once.

The material is editor work (human-only in this project). About ten minutes:

1. **Create it.** In the Content Browser, make a folder `UI` under `Content`.
   Right-click in it, **Material**, name it exactly `M_TeamOutline`, and open it.
2. **Make it a post-process.** Select the main material node. In Details set
   **Material Domain** to *Post Process* and **Blendable Location** to
   *Before Tonemapping*.
3. **Its three settings.** Add a **Vector Parameter** named `AllyColour`
   (default 0.2, 0.55, 1.0), a **Vector Parameter** named `EnemyColour`
   (1.0, 0.18, 0.15), and a **Scalar Parameter** named `Thickness` (2). The
   names must be exact: the game sets the two colours, and the colour-blind
   option turns red to orange.
4. **Where one pixel is.** Add **Screen Position** (use its *ViewportUV*
   output) and **View Size**. Add **Divide** with 1 on top and View Size below:
   that is one pixel's size. **Multiply** it by `Thickness`: call this *Step*.
5. **Five looks at the stencil.** Add five **SceneTexture** nodes, each with
   **Scene Texture Id** set to *CustomStencil*. Feed their UVs with:
   - the centre: *ViewportUV* as it is;
   - right: *ViewportUV* + (Step.x, 0), left: *ViewportUV* − (Step.x, 0);
   - down: *ViewportUV* + (0, Step.y), up: *ViewportUV* − (0, Step.y).
   (Use **Append**/**Component Mask** to build each offset, and **Add** or
   **Subtract**.) From each, take the **R** of its *Color* output.
6. **Is this pixel on an edge?** **Max** the four neighbours together (three
   Max nodes): call it *Around*. The pixel is on an outline when the centre is
   empty and something is around it: `Mask = (1 − Step(0.5, Centre)) ×
   Step(0.5, Around)`. Use **Step** nodes for the two steps and **OneMinus**,
   **Multiply**.
7. **Which colour.** **Lerp** from `AllyColour` to `EnemyColour` by
   **Saturate**(Around − 1): an enemy's stencil is 2, an ally's 1.
8. **Draw it over the picture.** Add a **SceneTexture** with Id
   *PostProcessInput0* (its *Color*). **Lerp** from it to the colour from step
   7 by *Mask*, and plug that into **Emissive Color**.
9. **Save**, close the editor, and run the game. The log line saying there is
   no outline goes away, and each unit has a thin line in its side's colour.

The outline shows through walls, since the stencil is drawn whatever is in
front of it. Atlas Reactor does the same, and it helps find a unit behind cover.
If you would rather it did not, multiply *Mask* by a comparison of
**SceneDepth** and **CustomDepth** so it only shows where the unit is in front.
