{
    "Source" : "RayTracedShadowsFullscreen.azsl",

    "DepthStencilState" :
    {
        "Depth" :
        {
            "Enable" : false
        },
        "Stencil" :
        {
            "Enable" : false
        }
    },

    // Multiplicative blend: dest = dest * source. A lit pixel outputs white (no change);
    // a shadowed pixel outputs the shadow factor, darkening the lighting target.
    "GlobalTargetBlendState" : {
        "Enable" : true,
        "BlendSource" : "Zero",
        "BlendDest" : "ColorSource",
        "BlendOp" : "Add"
    },

    "DrawList" : "forward",

    "ProgramSettings":
    {
      "EntryPoints":
      [
        {
          "name": "MainVS",
          "type": "Vertex"
        },
        {
          "name": "MainPS",
          "type": "Fragment"
        }
      ]
    }
}
