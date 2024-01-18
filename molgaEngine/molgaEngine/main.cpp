#include <iostream>
#include "Application.h"

int main(int argc, char* argv[]) {
    Application app;
    if (!app.Initalize()) {
        std::cerr << "Failed to initialize the application." << std::endl;
        return -1;
    }

    return 0;
}
