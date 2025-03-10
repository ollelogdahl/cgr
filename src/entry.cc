#include "application.h"

#include <iostream>

int main(int argc, char **argv) {
    bool validation = false;
    char *scene_file_path = nullptr;
    for (auto i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--validate") == 0) {
            validation = true;
        } else {
            scene_file_path = argv[i];
        }
    }

    if (!scene_file_path) {
        std::cerr << "usage: " << argv[0] << " [options] <scene file>" << std::endl;
        std::cerr << "options:" << std::endl;
        std::cerr << "  --validate    enable validation layers" << std::endl;
        return 1;
    }

    Application app;
    app.init_and_run(scene_file_path, {
        .validation_layers = validation,
    });

    return 0;
}
