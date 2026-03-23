#ifndef MOLGA_SHADER_H
#define MOLGA_SHADER_H

#include <glad/glad.h>
#include <string>

class Shader {
public:
    Shader(const char* vertexPath, const char* fragmentPath);
    ~Shader();

    void Use() const;

    void SetInt(const char* name, int value) const;
    void SetFloat(const char* name, float value) const;
    void SetVec2(const char* name, float x, float y) const;
    void SetVec3(const char* name, float x, float y, float z) const;
    void SetVec4(const char* name, float x, float y, float z, float w) const;
    void SetMat4(const char* name, const float* matrix) const;
    void SetBool(const char* name, bool value) const;

    unsigned int GetID() const { return programID; }

private:
    unsigned int programID;

    std::string LoadShaderSource(const char* path);
    unsigned int CompileShader(const char* source, GLenum type);
    void CheckCompileErrors(unsigned int shader, const std::string& type);
};

#endif // MOLGA_SHADER_H
