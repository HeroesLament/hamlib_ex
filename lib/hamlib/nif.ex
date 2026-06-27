defmodule Hamlib.Nif do
  @moduledoc """
  Raw NIF surface over the Hamlib C API (via the `hlx_*` C shim and a Rustler
  crate at `native/hamlib_nif`).

  This is the low-level binding — prefer `Hamlib.Rig` for a managed,
  process-owned interface. Every function here corresponds 1:1 to a shim call.

  ## Return shapes

    * Operations that don't return data → `:ok | {:error, {code, reason}}`
    * `get_freq/1` → `{:ok, hz} | {:error, {code, reason}}`
    * `get_mode/1` → `{:ok, {mode_string, passband_hz}} | {:error, …}`
    * `get_ptt/1`  → `{:ok, boolean} | {:error, …}`

  `code` is the integer Hamlib error (negative), `reason` its text. A handle is
  an opaque resource from `init/1`; the BEAM frees the underlying rig when the
  handle is garbage-collected (close + cleanup), even on owner crash.

  ## Lifecycle

      {:ok, h} = Hamlib.Nif.init(model)            # allocate (no I/O)
      :ok = Hamlib.Nif.set_conf(h, "rig_pathname", path)
      :ok = Hamlib.Nif.set_conf(h, "serial_speed", "19200")
      :ok = Hamlib.Nif.open(h)                      # opens the port
      ...
      :ok = Hamlib.Nif.close(h)
  """

  use Rustler, otp_app: :hamlib_ex, crate: "hamlib_nif"

  @typedoc "Opaque rig handle resource (from `init/1`)."
  @type handle :: reference()

  @typedoc "`{hamlib_error_code, reason_string}`."
  @type error :: {integer(), String.t()}

  @doc "Allocate + initialize a rig for a Hamlib model number. No port I/O."
  @spec init(integer()) :: {:ok, handle()} | {:error, atom()}
  def init(_model), do: :erlang.nif_error(:nif_not_loaded)

  @doc "Set a Hamlib config token by name (e.g. \"rig_pathname\", \"serial_speed\", \"ptt_type\")."
  @spec set_conf(handle(), String.t(), String.t()) :: :ok | {:error, error() | atom()}
  def set_conf(_handle, _token, _value), do: :erlang.nif_error(:nif_not_loaded)

  @doc "Open the configured rig port."
  @spec open(handle()) :: :ok | {:error, error()}
  def open(_handle), do: :erlang.nif_error(:nif_not_loaded)

  @doc "Close the rig port (rig stays initialized; reopenable)."
  @spec close(handle()) :: :ok | {:error, error()}
  def close(_handle), do: :erlang.nif_error(:nif_not_loaded)

  @doc "Set frequency in Hz on the current VFO."
  @spec set_freq(handle(), float()) :: :ok | {:error, error()}
  def set_freq(_handle, _freq_hz), do: :erlang.nif_error(:nif_not_loaded)

  @doc "Get frequency in Hz from the current VFO."
  @spec get_freq(handle()) :: {:ok, float()} | {:error, error()}
  def get_freq(_handle), do: :erlang.nif_error(:nif_not_loaded)

  @doc "Set mode by string + passband Hz (0 = rig's normal width)."
  @spec set_mode(handle(), String.t(), integer()) :: :ok | {:error, error() | atom()}
  def set_mode(_handle, _mode, _passband_hz), do: :erlang.nif_error(:nif_not_loaded)

  @doc "Get current `{mode_string, passband_hz}`."
  @spec get_mode(handle()) :: {:ok, {String.t(), integer()}} | {:error, error()}
  def get_mode(_handle), do: :erlang.nif_error(:nif_not_loaded)

  @doc "Set PTT: `true` = transmit, `false` = receive."
  @spec set_ptt(handle(), boolean()) :: :ok | {:error, error()}
  def set_ptt(_handle, _on), do: :erlang.nif_error(:nif_not_loaded)

  @doc "Get PTT state as a boolean."
  @spec get_ptt(handle()) :: {:ok, boolean()} | {:error, error()}
  def get_ptt(_handle), do: :erlang.nif_error(:nif_not_loaded)

  @doc "Set Hamlib's global debug verbosity (0 = silent … 5 = trace)."
  @spec set_debug(0..5) :: :ok
  def set_debug(_level), do: :erlang.nif_error(:nif_not_loaded)

  @doc "Hamlib library version string."
  @spec version() :: String.t()
  def version, do: :erlang.nif_error(:nif_not_loaded)
end
