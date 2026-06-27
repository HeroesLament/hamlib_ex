# Host smoke test: drive the Hamlib dummy rig (model 1) through the full
# Elixir stack — Hamlib.Rig GenServer -> Hamlib.Nif -> C shim -> libhamlib.
# Run: mix run smoke.exs

IO.puts("hamlib version: #{Hamlib.version()}")

{:ok, rig} =
  Hamlib.Rig.start_link(
    model: Hamlib.model(:dummy),
    conf: %{}
  )

IO.puts("started rig: #{inspect(rig)}")

IO.puts("set_freq 14.074 MHz: #{inspect(Hamlib.Rig.set_freq(rig, 14_074_000))}")
IO.puts("get_freq:            #{inspect(Hamlib.Rig.get_freq(rig))}")

IO.puts("set_mode PKTUSB:     #{inspect(Hamlib.Rig.set_mode(rig, "PKTUSB", 0))}")
IO.puts("get_mode:            #{inspect(Hamlib.Rig.get_mode(rig))}")

IO.puts("set_ptt ON:          #{inspect(Hamlib.Rig.set_ptt(rig, true))}")
IO.puts("get_ptt:             #{inspect(Hamlib.Rig.get_ptt(rig))}")
IO.puts("set_ptt OFF:         #{inspect(Hamlib.Rig.set_ptt(rig, false))}")
IO.puts("get_ptt:             #{inspect(Hamlib.Rig.get_ptt(rig))}")

IO.puts("close:               #{inspect(Hamlib.Rig.close(rig))}")
IO.puts("done")
