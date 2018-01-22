#pragma once

#include <string>

struct Shader;

struct Colormap
{
    std::string ID;
    std::array<float,3> center;
    float radius;
    Shader* shader;

    Colormap();

    void displaySettings();
    void getRange(float& min, float& max, int n) const;
    std::array<float, 3> getScale() const;
    std::array<float, 3> getBias() const;

    void autoCenterAndRadius(float min, float max);

    void nextShader();
    void previousShader();
    std::string getShaderName() const;
};

