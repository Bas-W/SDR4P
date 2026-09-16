#version 310 es

layout(local_size_x = 1, local_size_y = 64) in;

layout(std430, binding = 0) readonly buffer WaveformBuffer {
    float samples[];
};

layout(rgba8, binding = 1) uniform writeonly highp image2D outputTexture;

uniform int sampleCount;
uniform float minVal;
uniform float maxVal;
uniform float overlapRatio;

void main() {
    ivec2 pixel = ivec2(gl_GlobalInvocationID.xy);
    ivec2 size = imageSize(outputTexture);

    if (pixel.x >= size.x || pixel.y >= size.y) {
        return;
    }

    vec4 backgroundColor = vec4(0.1, 0.1, 0.1, 1.0);
    vec4 waveformColor = vec4(0.1, 0.3, 0.8, 1.0);

    if (sampleCount <= 0 || maxVal <= minVal) {
        imageStore(outputTexture, pixel, backgroundColor);
        return;
    }

    float valueRange = maxVal - minVal;
    float centerValue = (minVal + maxVal) * 0.5;

    float samplesPerPixel = float(sampleCount) / float(size.x);

    float centerSampleF = (float(pixel.x) + 0.5) * samplesPerPixel;
    float startSampleF = centerSampleF - samplesPerPixel * (overlapRatio + 1.0) * 0.5;
    float endSampleF = centerSampleF + samplesPerPixel * (overlapRatio + 1.0) * 0.5;

    int startSample = clamp(int(floor(startSampleF)), 0, sampleCount - 1);
    int endSample = clamp(int(ceil(endSampleF)), startSample + 1, sampleCount);

    float binTotal = 0.0;
    float binMin = samples[startSample];
    float binMax = samples[startSample];

    float tempVal = 0.0;

    for (int i = startSample; i < endSample; i++) {
        tempVal = samples[i];
        binTotal += tempVal;
        if (tempVal < binMin){
            binMin = tempVal;
        }
        if (tempVal > binMax){
            binMax = tempVal;
        }
    }

    float normalizedAvg = (float(binTotal) / float(endSample - startSample) - minVal) / valueRange;
    float normalizedMin = (binMin - minVal) / valueRange;
    float normalizedMax = (binMax - minVal) / valueRange;
    float avgY = (1.0 - normalizedAvg) * float(size.y - 1);
    float waveformYMin = (1.0 - normalizedMin) * float(size.y - 1);
    float waveformYMax = (1.0 - normalizedMax) * float(size.y - 1);

    float centerNormalized = (centerValue - minVal) / valueRange;
    float centerY = (1.0 - centerNormalized) * float(size.y - 1);

    vec4 color = backgroundColor;

    if (float(pixel.y) >= waveformYMax && float(pixel.y) <= waveformYMin) {
        color = waveformColor;
    }

    imageStore(outputTexture, pixel, color);
}