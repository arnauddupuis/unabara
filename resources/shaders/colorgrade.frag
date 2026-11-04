#version 440

// Unabara colour-grade preview shader (v0.4 track C).
//
// Mirror of GradeOps::apply() (src/core/color_grade.cpp) — keep in sync.
// GradeOps::apply() is the ground truth of the colour pipeline; this shader
// reproduces it op for op, including every clamp, in GPU float precision
// (same discipline as the 1.8 font factor duplicated in overlay_gen.cpp /
// the QML previews). Fixed op order:
//   1. 3×3 matrix, output clamped to [0,1]
//   2. black point: x = clamp((x − b)/(1 − b), 0, 1)
//   3. midtone lift: x = clamp(x + g·4x²(1−x), 0, 1)
//   4. S-contrast about 0.5 (exponent c): x<0.5 → 0.5·(2x)^c,
//      else 1 − 0.5·(2(1−x))^c — maps [0,1]→[0,1], no clipping
//   5. optional user LUT (uLutEnabled), sampled from the Hald-style strip
// apply() guards ops 2–4 behind "is this op a no-op" conditionals; those
// are mathematically identity when skipped (b=0, g=0, c=1), so this shader
// computes them unconditionally.
//
// LUT strip addressing (must match LutStripImageProvider::identityStrip and
// the image://lutstrip/ provider): N slices of N×N laid side by side
// (strip is N²×N, e.g. 1089×33 for N=33); pixel (b·N + r, g) holds the LUT
// entry for input (r, g, b), red fastest horizontally inside a slice, green
// down, blue = slice index. In-slice red/green interpolation is the
// sampler's own bilinear filtering (coordinates are scaled by (N−1)/strip
// size and offset half a texel so filtering never bleeds across slice
// edges); blue interpolates linearly between the two neighbouring slices.
// This bilinear-via-two-slices lookup is NOT the tetrahedral interpolation
// the export path uses (CubeLut::sample / FFmpeg lut3d:interp=tetrahedral)
// — tetrahedral is impractical in a strip lookup — so the preview can
// differ from the export by a ≤1-LSB-class residual (plus the strip's
// 8-bit quantisation). The plan accepts preview residuals of this class.
//
// Split preview: the GRADED result is shown LEFT of uSplitPos (normalized
// item x in [0,1]); the original shows to the right. uSplitPos = 1.0 means
// fully graded (ColorPanel's default). Bypass (hold-to-compare) is NOT a
// shader uniform: VideoSyncPlayer deactivates the whole effect instead
// (visible:false + hideSource:false), which shows the pristine VideoOutput
// at zero shader cost — cheaper and exactly original by construction.

layout(location = 0) in vec2 qt_TexCoord0;
layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    // Member names must match the ShaderEffect's QML property names.
    vec4 uMatR;       // matrix row 0 (xyz used)
    vec4 uMatG;       // matrix row 1
    vec4 uMatB;       // matrix row 2
    vec4 uBlack;      // per-channel black point (xyz)
    vec4 uLift;       // per-channel midtone lift g (xyz)
    float uContrast;  // S-curve exponent, >= 1
    float uSplitPos;  // graded left of this normalized x; 1.0 = fully graded
    float uLutEnabled; // > 0.5 = sample the user LUT strip
    float uLutSize;   // LUT edge size N (strip is N² wide, N tall)
};

layout(binding = 1) uniform sampler2D source;   // ShaderEffectSource (video)
layout(binding = 2) uniform sampler2D lutStrip; // image://lutstrip/ Hald strip

float scurve(float x, float c)
{
    // x is already clamped to [0,1] by op 3, so pow() sees no negatives.
    return x < 0.5 ? 0.5 * pow(2.0 * x, c)
                   : 1.0 - 0.5 * pow(2.0 * (1.0 - x), c);
}

vec3 sampleLutStrip(vec3 c)
{
    float n = uLutSize;
    float scale = n - 1.0;
    float bf = c.b * scale;
    float b0 = floor(bf);
    float f = bf - b0;
    float b1 = min(b0 + 1.0, scale);
    float v = (c.g * scale + 0.5) / n;
    float stripW = n * n;
    float u0 = (b0 * n + c.r * scale + 0.5) / stripW;
    float u1 = (b1 * n + c.r * scale + 0.5) / stripW;
    vec3 s0 = texture(lutStrip, vec2(u0, v)).rgb;
    vec3 s1 = texture(lutStrip, vec2(u1, v)).rgb;
    return mix(s0, s1, f);
}

void main()
{
    vec4 tex = texture(source, qt_TexCoord0);
    // Video frames are opaque (alpha = 1), so the premultiplied texture's
    // rgb is the straight colour; alpha is passed through untouched.
    vec3 c = tex.rgb;

    if (qt_TexCoord0.x <= uSplitPos) {
        // 1. matrix (clamped like colorchannelmixer's 8-bit output)
        vec3 m = vec3(dot(uMatR.xyz, c), dot(uMatG.xyz, c), dot(uMatB.xyz, c));
        m = clamp(m, 0.0, 1.0);
        // 2. black point
        m = clamp((m - uBlack.xyz) / (vec3(1.0) - uBlack.xyz), 0.0, 1.0);
        // 3. midtone lift x + g·4x²(1−x), clamped before the S-curve
        m = clamp(m + uLift.xyz * 4.0 * m * m * (vec3(1.0) - m), 0.0, 1.0);
        // 4. S-contrast about 0.5
        m = vec3(scurve(m.r, uContrast), scurve(m.g, uContrast), scurve(m.b, uContrast));
        // 5. optional user LUT, applied AFTER the parametric grade
        if (uLutEnabled > 0.5)
            m = sampleLutStrip(m);
        c = m;
    }

    fragColor = vec4(c, tex.a) * qt_Opacity;
}
