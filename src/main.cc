#include "application.h"

#include <iostream>

int main(int argc, char **argv) {

    if (argc < 2) {
        std::cerr << "usage: " << argv[0] << " <scene file>" << std::endl;
        return 1;
    }

    Application app;
    app.init_and_run(argv[1]);
}
