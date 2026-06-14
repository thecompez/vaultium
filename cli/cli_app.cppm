export module vaultium_cli_app;

export namespace vaultium::cli {

    /**
     * @brief Runs Vaultium command line application.
     *
     * @param argc Argument count.
     * @param argv Argument values.
     * @return Exit code.
     */
    auto run(int argc, char* argv[]) -> int;

} // namespace vaultium::cli