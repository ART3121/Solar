# Fish completion for Solar.

function __solar_command_is
    set -l tokens (commandline -opc)
    test (count $tokens) -ge 2; and test "$tokens[2]" = "$argv[1]"
end

function __solar_needs_subcommand
    set -l tokens (commandline -opc)
    test (count $tokens) -eq 2; and test "$tokens[2]" = "$argv[1]"
end

function __solar_subcommand_is
    set -l tokens (commandline -opc)
    test (count $tokens) -ge 3; and test "$tokens[2]" = "$argv[1]"; and test "$tokens[3]" = "$argv[2]"
end

function __solar_candidates
    set -l tokens (commandline -opc)
    set -l executable solar
    if test (count $tokens) -ge 1
        set executable "$tokens[1]"
    end
    command $executable __complete "$argv[1]" 2>/dev/null
end

complete -c solar -f
complete -c solar -n 'test (count (commandline -opc)) -eq 1' \
    -a 'init scan config check doctor clean build view report'
complete -c solar -n 'test (count (commandline -opc)) -eq 1' -l help
complete -c solar -n 'test (count (commandline -opc)) -eq 1' -l version

complete -c solar -n '__solar_command_is init' -l template -r -a 'verilog sapho'
complete -c solar -n '__solar_command_is init' -l help
complete -c solar -n '__solar_command_is scan' -l help
complete -c solar -n '__solar_command_is check' -l help

complete -c solar -n '__solar_needs_subcommand config' -a set
complete -c solar -n '__solar_command_is config' -l help
complete -c solar -n '__solar_subcommand_is config set' -l name -r
complete -c solar -n '__solar_subcommand_is config set' -l top -r
complete -c solar -n '__solar_subcommand_is config set' -l test -r \
    -a '(__solar_candidates tests)'

complete -c solar -n '__solar_command_is doctor' -l all
complete -c solar -n '__solar_command_is doctor' -l help
complete -c solar -n '__solar_command_is clean' -l cache
complete -c solar -n '__solar_command_is clean' -l all
complete -c solar -n '__solar_command_is clean' -l help

complete -c solar -n '__solar_needs_subcommand build' -a 'rtl sim synth full'
complete -c solar -n '__solar_command_is build' -l help
complete -c solar -n '__solar_subcommand_is build rtl; or __solar_subcommand_is build sim; or __solar_subcommand_is build synth; or __solar_subcommand_is build full' \
    -l profile -r -a '(__solar_candidates profiles)'
complete -c solar -n '__solar_subcommand_is build rtl; or __solar_subcommand_is build sim; or __solar_subcommand_is build synth; or __solar_subcommand_is build full' \
    -l dry-run
complete -c solar -n '__solar_subcommand_is build sim' -l all
complete -c solar -n '__solar_subcommand_is build sim' -l list
complete -c solar -n '__solar_subcommand_is build sim' -l no-progress
complete -c solar -n '__solar_subcommand_is build sim' -l verbose
complete -c solar -n '__solar_subcommand_is build sim' -a '(__solar_candidates tests)'
complete -c solar -n '__solar_subcommand_is build full' -l no-progress
complete -c solar -n '__solar_subcommand_is build full' -l verbose

complete -c solar -n '__solar_command_is view' -l viewer -r -a 'gtkwave surfer'
complete -c solar -n '__solar_command_is view' -l waveform -r \
    -a '(__solar_candidates waveforms)'
complete -c solar -n '__solar_command_is view' -l help
complete -c solar -n '__solar_command_is view' -a '(__solar_candidates tests)'

complete -c solar -n '__solar_needs_subcommand report' -a 'show list compare'
complete -c solar -n '__solar_command_is report' -l help
complete -c solar -n '__solar_subcommand_is report show' \
    -a '(__solar_candidates build-ids)'
complete -c solar -n '__solar_subcommand_is report list' -l limit -r
complete -c solar -n '__solar_subcommand_is report compare' -l against -r \
    -a '(__solar_candidates build-ids)'
complete -c solar -n '__solar_subcommand_is report compare' -l summary
complete -c solar -n '__solar_subcommand_is report compare' \
    -a '(__solar_candidates build-ids)'
