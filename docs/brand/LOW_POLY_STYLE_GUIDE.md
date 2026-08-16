# Low-Poly Style Guide

## Target

The target is a deliberate late-1990s to early-2000s game-character construction with a modern readable silhouette. It is not a smooth modern model processed through a polygon filter.

## Production constraints

| Area | Initial production target |
|---|---:|
| Canonical model | 6,000–10,000 triangles |
| UI LOD | 2,000–4,000 triangles |
| Face texture | 512×512 |
| Body texture | 512×512 or 1024×1024 |
| Materials | 3–5 |
| Skeleton | 35–50 bones |
| Expressions | 6–10 blend shapes or equivalent |
| Idle sampling | 12–15 fps |

## Modeling

- Model the final silhouette directly at low polygon density.
- Hair uses solid polygon clumps rather than many alpha cards.
- Garment folds are simplified into geometry and hand-painted values.
- Do not sculpt a detailed high-poly asset and rely on automated decimation as the canonical workflow.
- Facial planes should remain visible but not obscure expression readability.

## Texture and shading

- hand-painted albedo;
- limited or no normal mapping;
- restrained specular response;
- flat or limited Gouraud-style shading;
- no cinematic bloom or holographic edge treatment;
- optional dither only in presentation, not over UI text.

## Phase 1 blockouts

The generated 268-triangle blockouts are silhouette/topology fixtures only. They intentionally sit far below final production density and must not be mistaken for release models.
