set pagination off
set confirm off
set print pretty on
set print elements 0
set breakpoint pending on

handle SIGPIPE nostop noprint pass
handle SIGUSR1 nostop noprint pass
handle SIGUSR2 nostop noprint pass

break abort
break __assert_fail
catch signal SIGSEGV
catch signal SIGABRT

define simstate
  printf "sim_time=%lld\n", sim_time
  printf "exit_reason=%d\n", cpu.ctx.exit_reason
end

document simstate
Print the global simulator cycle and exit reason.
end

define simbt
  bt
  frame 0
  info args
  info locals
end

document simbt
Show a short backtrace plus arguments and locals for the current frame.
end
