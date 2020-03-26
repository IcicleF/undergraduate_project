#include <commons.hpp>
#include <iostream>

DEFINE_MAIN_INFO()

int main(int argc, char **argv)
{
    COLLECT_MAIN_INFO();
    std::cout << "Dummy!" << std::endl;
    return 0;
}