#ifndef LIGHTING_GLSL
#define LIGHTING_GLSL

// Filament https://google.github.io/filament/Filament.md.html#listing_glslpunctuallight
float squareFalloffAttenuation(float distanceSquare, float lightInvRadius) {
    float factor = distanceSquare * lightInvRadius * lightInvRadius;
    float smoothFactor = max(1.0 - factor * factor, 0.0);
    return (smoothFactor * smoothFactor) / max(distanceSquare, 1e-4);
}

#endif
