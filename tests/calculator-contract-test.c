#define main infiltratoros_calculator_entry
#include "../packaging/install-calculator.c"
#undef main

/* Unit qualification uses isolated dpkg status stanzas; no root or APT. */
static void check_case(const char *path, const char *fixture,
                       const char *expected, bool should_match)
{
    FILE *stream = fopen(path, "wb");
    if (!stream) {
        perror("open Calculator status fixture");
        exit(EXIT_FAILURE);
    }
    if (fputs(fixture, stream) == EOF || fclose(stream) != 0) {
        fputs("Cannot write Calculator status fixture\n", stderr);
        exit(EXIT_FAILURE);
    }
    if (installed_package_version_matches(path, "calculator", expected) != should_match) {
        fputs("Incorrect installed Calculator version validation\n", stderr);
        exit(EXIT_FAILURE);
    }
}

int main(void)
{
    char path[] = "/tmp/infiltratoros-calculator-contract-XXXXXX";
    int fd = mkstemp(path);
    if (fd < 0) return EXIT_FAILURE;
    close(fd);
    check_case(path,
        "Package: other\nVersion: 0.2.64\nStatus: install ok installed\n\n"
        "Package: calculator\nStatus: install ok installed\nVersion: 0.2.64\n",
        "0.2.64", true);
    check_case(path, "Package: calculator\nVersion: 0.2.63\nStatus: install ok installed\n",
        "0.2.64", false);
    check_case(path, "Package: calculator\nVersion: 0.2.64\nStatus: deinstall ok config-files\n",
        "0.2.64", false);
    check_case(path,
        "Package: calculator\nVersion: 0.2.63\nStatus: install ok installed\n\n"
        "Package: other\nVersion: 0.2.64\nStatus: install ok installed\n",
        "0.2.64", false);
    check_case(path, "Package: other\nVersion: 0.2.64\nStatus: install ok installed\n",
        "0.2.64", false);
    unlink(path);
    if (installed_package_version_matches(path, "calculator", "0.2.64"))
        return EXIT_FAILURE;
    puts("Calculator installed-version contract: verified");
    return EXIT_SUCCESS;
}
