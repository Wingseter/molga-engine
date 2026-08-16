#include "Shader.h"
#include <fstream>
#include <sstream>
#include <iostream>

Shader::Shader(const char* vertexPath, const char* fragmentPath) 
    : vertexPath(vertexPath), fragmentPath(fragmentPath) {
    std::string vertexSource = LoadShaderSource(vertexPath);
    std::string fragmentSource = LoadShaderSource(fragmentPath);

    unsigned int vertexShader = CompileShader(vertexSource.c_str(), GL_VERTEX_SHADER);
    unsigned int fragmentShader = CompileShader(fragmentSource.c_str(), GL_FRAGMENT_SHADER);

    programID = glCreateProgram();
    glAttachShader(programID, vertexShader);
    glAttachShader(programID, fragmentShader);
    glLinkProgram(programID);
    valid_ = CheckCompileErrors(programID, "PROGRAM");

    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);
}

Shader::~Shader() {
    glDeleteProgram(programID);
}

void Shader::Use() const {
    glUseProgram(programID);
}

bool Shader::Reload() {
    std::string vertexSource = LoadShaderSource(vertexPath.c_str());
    std::string fragmentSource = LoadShaderSource(fragmentPath.c_str());

    if (vertexSource.empty() && !vertexPath.empty()) {
        std::cerr << "ERROR::SHADER::RELOAD::FAILED_TO_LOAD_VERTEX_SOURCE: " << vertexPath << std::endl;
        return false;
    }
    if (fragmentSource.empty() && !fragmentPath.empty()) {
        std::cerr << "ERROR::SHADER::RELOAD::FAILED_TO_LOAD_FRAGMENT_SOURCE: " << fragmentPath << std::endl;
        return false;
    }

    unsigned int vertexShader = glCreateShader(GL_VERTEX_SHADER);
    const char* vertSrc = vertexSource.c_str();
    glShaderSource(vertexShader, 1, &vertSrc, nullptr);
    glCompileShader(vertexShader);
    if (!CheckCompileErrors(vertexShader, "VERTEX")) {
        glDeleteShader(vertexShader);
        return false;
    }

    unsigned int fragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
    const char* fragSrc = fragmentSource.c_str();
    glShaderSource(fragmentShader, 1, &fragSrc, nullptr);
    glCompileShader(fragmentShader);
    if (!CheckCompileErrors(fragmentShader, "FRAGMENT")) {
        glDeleteShader(vertexShader);
        glDeleteShader(fragmentShader);
        return false;
    }

    unsigned int newProgramID = glCreateProgram();
    glAttachShader(newProgramID, vertexShader);
    glAttachShader(newProgramID, fragmentShader);
    glLinkProgram(newProgramID);

    if (!CheckCompileErrors(newProgramID, "PROGRAM")) {
        glDeleteShader(vertexShader);
        glDeleteShader(fragmentShader);
        glDeleteProgram(newProgramID);
        return false;
    }

    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);

    glDeleteProgram(programID);
    programID = newProgramID;
    valid_ = true;
    uniformCache.clear();

    return true;
}

GLint Shader::GetUniformLocation(const char* name) const {
    auto it = uniformCache.find(name);
    if (it != uniformCache.end()) {
        return it->second;
    }
    GLint location = glGetUniformLocation(programID, name);
    uniformCache[name] = location;
    return location;
}

void Shader::SetInt(const char* name, int value) const {
    glUniform1i(GetUniformLocation(name), value);
}

void Shader::SetUInt(const char* name, unsigned int value) const {
    glUniform1ui(GetUniformLocation(name), value);
}

void Shader::SetFloat(const char* name, float value) const {
    glUniform1f(GetUniformLocation(name), value);
}

void Shader::SetVec2(const char* name, float x, float y) const {
    glUniform2f(GetUniformLocation(name), x, y);
}

void Shader::SetVec3(const char* name, float x, float y, float z) const {
    glUniform3f(GetUniformLocation(name), x, y, z);
}

void Shader::SetVec4(const char* name, float x, float y, float z, float w) const {
    glUniform4f(GetUniformLocation(name), x, y, z, w);
}

void Shader::SetMat4(const char* name, const float* matrix) const {
    glUniformMatrix4fv(GetUniformLocation(name), 1, GL_FALSE, matrix);
}

void Shader::SetBool(const char* name, bool value) const {
    glUniform1i(GetUniformLocation(name), static_cast<int>(value));
}

std::string Shader::LoadShaderSource(const char* path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        std::cerr << "ERROR::SHADER::FILE_NOT_FOUND: " << path << std::endl;
        return "";
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

unsigned int Shader::CompileShader(const char* source, GLenum type) {
    unsigned int shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);

    std::string typeName = (type == GL_VERTEX_SHADER) ? "VERTEX" : "FRAGMENT";
    CheckCompileErrors(shader, typeName);

    return shader;
}

bool Shader::CheckCompileErrors(unsigned int shader, const std::string& type) {
    int success;
    char infoLog[1024];

    if (type != "PROGRAM") {
        glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
        if (!success) {
            glGetShaderInfoLog(shader, 1024, nullptr, infoLog);
            std::cerr << "ERROR::SHADER::" << type << "::COMPILATION_FAILED\n" << infoLog << std::endl;
            return false;
        }
    } else {
        glGetProgramiv(shader, GL_LINK_STATUS, &success);
        if (!success) {
            glGetProgramInfoLog(shader, 1024, nullptr, infoLog);
            std::cerr << "ERROR::SHADER::PROGRAM::LINKING_FAILED\n" << infoLog << std::endl;
            return false;
        }
    }
    return true;
}
