#version 430

// @TODO: add configurable segments (# bars, freq range per bar)

layout(local_size_x = 1, local_size_y = 64) in;

layout(std430, binding = 0) readonly buffer WaveformBuffer {
    float samples[];
};

layout(rgba8, binding = 1) uniform writeonly image2D outputTexture;

uniform int sampleCount;
uniform float minVal;
uniform float maxVal;

void main() {
    ivec2 pixel = ivec2(gl_GlobalInvocationID.xy);
    ivec2 size = imageSize(outputTexture);

    if (pixel.x >= size.x || pixel.y >= size.y) {
        return;
    }

    vec4 backgroundColor = vec4(0.1, 0.1, 0.1, 1.0);
    vec4 lineColor = vec4(0.1, 0.3, 0.8, 1.0);
    vec4 color = backgroundColor;

    if (sampleCount <= 0 || maxVal <= minVal) {
        imageStore(outputTexture, pixel, backgroundColor);
        return;
    }

    float valueRange = maxVal - minVal;

    float samplesPerPixel = float(sampleCount) / float(size.x);

    if (samplesPerPixel > 1.0f) {
        float centerSampleF = (float(pixel.x) + 0.5) * samplesPerPixel;
        float startSampleF = centerSampleF - samplesPerPixel * 0.5;
        float endSampleF = centerSampleF + samplesPerPixel * 0.5;

        int startSample = clamp(int(floor(startSampleF)), 0, sampleCount - 1);
        int endSample = clamp(int(ceil(endSampleF)), startSample + 1, sampleCount);

        float binMin = samples[startSample];
        float binMax = samples[startSample];

        float tempVal = 0.0;

        for (int i = startSample; i < endSample; i++) {
            tempVal = samples[i];
            if (tempVal < binMin){
                binMin = tempVal;
            }
            if (tempVal > binMax){
                binMax = tempVal;
            }
        }

        float normalizedMin = (binMin - minVal) / valueRange;
        float normalizedMax = (binMax - minVal) / valueRange;
        float yMin = (1.0 - normalizedMin) * float(size.y - 1);
        float yMax = (1.0 - normalizedMax) * float(size.y - 1);

        if (pixel.y >= yMax && pixel.y <= yMin) {
            color = lineColor;
        }

    } else {
        float centerSampleF = float(sampleCount) / float(size.x) * float(pixel.x);
        int sampleLo = int(floor(centerSampleF));
        int sampleHi = int(ceil(centerSampleF));

        float valLo = samples[sampleLo];
        float valHi = samples[sampleHi];

        vec2 pxLo = vec2(float(sampleLo) / float(sampleCount) * float(size.x), (1.0f - (valLo - minVal) / valueRange) * float(size.y - 1));
        vec2 pxHi = vec2(float(sampleHi) / float(sampleCount) * float(size.x), (1.0f - (valHi - minVal) / valueRange) * float(size.y - 1));

        float dist = abs((pxHi.y - pxLo.y) * float(pixel.x) - (pxHi.x - pxLo.x) * float(pixel.y) + pxHi.x * pxLo.y - pxHi.y * pxLo.x) /
        sqrt((pxHi.y - pxLo.y) * (pxHi.y - pxLo.y) + (pxHi.x - pxLo.x) * (pxHi.x - pxLo.x));

        if (dist <= 0.6f){
            color = lineColor;
        }
    }

    imageStore(outputTexture, pixel, color);
}