#include "chen/application.h"

int main(int argc, char** argv) {
    chen::Application app;
    if (app.init(argc, argv)) {
        return app.run();
    }
    return 0;
}