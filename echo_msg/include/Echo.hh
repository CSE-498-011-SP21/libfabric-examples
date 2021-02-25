#include <iostream>

#define ERRCHK(x) error_check((x), __FILE__, __LINE__);

inline void error_check(int err, std::string file, int line) {
    if (err) {
        std::cerr << "ERROR (" << err << "): " << fi_strerror(-err) << std::endl;
        std::cerr << file << ":" << line << std::endl;

        exit(1);
    }
}