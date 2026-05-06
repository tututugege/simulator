set pagination off
set confirm off
set print pretty on
set print elements 0
set breakpoint pending on

handle SIGPIPE nostop noprint pass
handle SIGUSR1 nostop noprint pass
handle SIGUSR2 nostop noprint pass

catch signal SIGSEGV
commands
  printf "=== CRASH: SIGSEGV caught ===\n"
  bt full
  info registers
  frame 0
  info args
  info locals
  quit
end

catch signal SIGABRT
commands
  printf "=== CRASH: SIGABRT caught ===\n"
  bt full
  info registers
  frame 0
  info args
  info locals
  quit
end

break abort
commands
  printf "=== CRASH: abort() called ===\n"
  bt full
  info registers
  frame 0
  info args
  info locals
  quit
end

break __assert_fail
commands
  printf "=== CRASH: __assert_fail() called ===\n"
  bt full
  info registers
  frame 0
  info args
  info locals
  quit
end

run
