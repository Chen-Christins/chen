#include "../src/Application.hpp"
#include "../src/Log/Log.hpp"

static chen::Logger::ptr logger = LOG_ROOT();

int main(int argc, char** argv)
{
    try {
        srand(time(0));
        chen::Application app;
        if (app.init(argc, argv)) {
            app.run();
        }
    } catch (const std::exception& e) {
        ERROR(logger) << "Exception: " << e.what();
    }

    return 0;
}