defmodule Hamlib do
  @moduledoc """
  Elixir bindings for [Hamlib](https://hamlib.github.io/), the Ham Radio
  Control Library.

  This module is the convenience entry point. For controlling an actual radio,
  use `Hamlib.Rig`, which owns a rig handle in a process (one controlling
  process per serial line). For the raw 1:1 binding, see `Hamlib.Nif`.

  ## Model numbers

  Hamlib identifies each supported radio by an integer model number (e.g. 1 is
  the dummy/test rig, 2 is NET rigctl, the Icom/Yaesu/Kenwood/Elecraft families
  occupy their own number ranges). `Hamlib.Rig.start_link/1` takes a `:model`.
  A few common ones are exposed as `model/1` shortcuts; for the rest, pass the
  integer directly (see `rigctl -l` or the Hamlib `riglist.h`).
  """

  @doc "The Hamlib library version string, e.g. \"Hamlib 4.6.5\"."
  @spec version() :: String.t()
  defdelegate version(), to: Hamlib.Nif

  @doc """
  Set Hamlib's global debug verbosity. The NIF defaults this to `0` (silent) at
  load to keep Hamlib's chatty stdout logging out of the BEAM console; raise it
  when diagnosing rig issues.

      Hamlib.set_debug(4)  # verbose
  """
  @spec set_debug(0..5) :: :ok
  defdelegate set_debug(level), to: Hamlib.Nif

  @doc """
  Convenience model-number lookup for a few common targets. Returns the integer
  Hamlib model number. Pass the integer directly for anything not listed.

    * `:dummy`   — model 1, the virtual test rig (no hardware)
    * `:netrigctl` — model 2, talk to an external `rigctld` over TCP
  """
  @spec model(atom()) :: integer()
  def model(:dummy), do: 1
  def model(:netrigctl), do: 2
end
