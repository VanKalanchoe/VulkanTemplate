dxc -T vs_6_0 -spirv -Fo TexturedQuad.vert.spv TexturedQuad.vert
dxc -T ps_6_0 -spirv -Fo SolidColorDepth.frag.spv SolidColorDepth.frag
dxc -T vs_6_0 -spirv -Fo PositionColorTransform.vert.spv PositionColorTransform.vert
dxc -T ps_6_0 -spirv -Fo DepthOutline.frag.spv DepthOutline.frag
dxc -T cs_6_0 -spirv -Fo shader.comp.spv SpriteBatch.comp
pause