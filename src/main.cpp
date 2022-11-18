#include <vk_engine.h>

#include <Colors.h>
#include <iostream>

int main(int argc, char* argv[])
{
    /* Ignore unused arguments */
    (void)argc;
    (void)argv;
    try
    {
        VulkanEngine engine;
        engine.run();
    }
    catch (const FatalError& e)
    {
        std::cerr << COLOR_UTF8_RED << "Fatal error: " << e.what() << COLOR_UTF8_RESET << std::endl;
    }
    catch (const std::exception& e)
    {
        std::cerr << COLOR_UTF8_RED << "std::exception: " << e.what() << COLOR_UTF8_RESET << std::endl;
    }

    return 0;
}
