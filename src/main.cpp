#include <vk_engine.h>

#include <Colors.h>
#include <iostream>

int main(int argc, const char* argv[])
{
    std::ignore = argc;
    std::ignore = argv;
    try
    {
        for (;;)
            runEngine();
    }
    catch (const QuitException&) {}
    catch (const FatalError& e)
    {
        std::cerr << COLOR_UTF8_RED << "Fatal error: " << e.what() << COLOR_UTF8_RESET << std::endl;
        return 1;
    }
    catch (const std::exception& e)
    {
        std::cerr << COLOR_UTF8_RED << "std::exception: " << e.what() << COLOR_UTF8_RESET << std::endl;
        return 1;
    }

    return 0;
}
