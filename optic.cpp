/* optic.cpp */

#include "core/core.hpp"

int main(int argc, char *argv[]) {
    OpticCore optic_core;
    return optic_core.start_core(--argc, ++argv);
}