defmodule Hamlib.Rig do
  @moduledoc """
  A process-owned Hamlib rig.

  Wraps a `Hamlib.Nif` handle in a `GenServer` so that:

    * one process owns the serial line (single controlling process — the right
      model for a UART, and what a consuming app's rig-control layer wants),
    * commands are serialized (no two callers driving the rig at once),
    * the port is opened on start and closed/cleaned up on terminate.

  ## Starting

      {:ok, rig} =
        Hamlib.Rig.start_link(
          model: Hamlib.model(:dummy),
          conf: %{}              # config tokens applied before open
        )

  For a real serial rig:

      {:ok, rig} =
        Hamlib.Rig.start_link(
          model: 3073,           # e.g. an Icom CI-V model number
          conf: %{
            "rig_pathname" => "/dev/tty.usbserial-XXXX",
            "serial_speed" => "19200",
            "ptt_type"     => "RTS"
          }
        )

  For NET rigctl (talk to an external `rigctld`):

      {:ok, rig} =
        Hamlib.Rig.start_link(
          model: Hamlib.model(:netrigctl),
          conf: %{"rig_pathname" => "127.0.0.1:4532"}
        )

  ## Android / DigiRig note

  On Android there is no `/dev/tty*` for USB serial; the serial path Hamlib
  expects must be bridged to the Android USB host API. That bridge is tracked
  separately; the API here is identical regardless of how the bytes reach the
  radio.
  """

  use GenServer

  require Logger

  @type option ::
          {:model, integer()}
          | {:conf, %{optional(String.t()) => String.t()}}
          | {:name, GenServer.name()}
          | {:open, boolean()}

  # ── Client API ───────────────────────────────────────────────────────────

  @doc """
  Start a rig process. Options:

    * `:model` (required) — Hamlib model number (see `Hamlib.model/1`).
    * `:conf` — map of Hamlib config tokens applied before opening the port.
    * `:open` — open the port on start (default `true`). `false` leaves the rig
      initialized + configured but closed, to open later with `open/1`.
    * `:name` — optional GenServer name.
  """
  @spec start_link([option()]) :: GenServer.on_start()
  def start_link(opts) do
    {name, opts} = Keyword.pop(opts, :name)
    gen_opts = if name, do: [name: name], else: []
    GenServer.start_link(__MODULE__, opts, gen_opts)
  end

  @doc "Open the rig port (if started with `open: false`, or after `close/1`)."
  @spec open(GenServer.server()) :: :ok | {:error, term()}
  def open(server), do: GenServer.call(server, :open)

  @doc "Close the rig port (rig stays initialized; reopen with `open/1`)."
  @spec close(GenServer.server()) :: :ok | {:error, term()}
  def close(server), do: GenServer.call(server, :close)

  @doc "Set frequency in Hz on the current VFO."
  @spec set_freq(GenServer.server(), number()) :: :ok | {:error, term()}
  def set_freq(server, freq_hz), do: GenServer.call(server, {:set_freq, freq_hz / 1.0})

  @doc "Get frequency in Hz from the current VFO. `{:ok, hz}`."
  @spec get_freq(GenServer.server()) :: {:ok, float()} | {:error, term()}
  def get_freq(server), do: GenServer.call(server, :get_freq)

  @doc "Set mode by string (\"USB\", \"PKTUSB\", …) and passband Hz (0 = normal)."
  @spec set_mode(GenServer.server(), String.t(), integer()) :: :ok | {:error, term()}
  def set_mode(server, mode, passband_hz \\ 0),
    do: GenServer.call(server, {:set_mode, mode, passband_hz})

  @doc "Get current `{mode_string, passband_hz}`."
  @spec get_mode(GenServer.server()) :: {:ok, {String.t(), integer()}} | {:error, term()}
  def get_mode(server), do: GenServer.call(server, :get_mode)

  @doc "Key/unkey the transmitter. `true` = TX, `false` = RX."
  @spec set_ptt(GenServer.server(), boolean()) :: :ok | {:error, term()}
  def set_ptt(server, on) when is_boolean(on), do: GenServer.call(server, {:set_ptt, on})

  @doc "Get PTT state as a boolean. `{:ok, true|false}`."
  @spec get_ptt(GenServer.server()) :: {:ok, boolean()} | {:error, term()}
  def get_ptt(server), do: GenServer.call(server, :get_ptt)

  # ── Server ───────────────────────────────────────────────────────────────

  @impl true
  def init(opts) do
    model = Keyword.fetch!(opts, :model)
    conf = Keyword.get(opts, :conf, %{})
    do_open = Keyword.get(opts, :open, true)

    with {:ok, handle} <- Hamlib.Nif.init(model),
         :ok <- apply_conf(handle, conf),
         :ok <- maybe_open(handle, do_open) do
      {:ok, %{handle: handle, model: model, conf: conf, open?: do_open}}
    else
      {:error, reason} ->
        {:stop, {:hamlib_init_failed, reason}}
    end
  end

  @impl true
  def handle_call(:open, _from, %{open?: true} = state) do
    {:reply, :ok, state}
  end

  def handle_call(:open, _from, state) do
    case Hamlib.Nif.open(state.handle) do
      :ok -> {:reply, :ok, %{state | open?: true}}
      err -> {:reply, err, state}
    end
  end

  def handle_call(:close, _from, %{open?: false} = state) do
    {:reply, :ok, state}
  end

  def handle_call(:close, _from, state) do
    case Hamlib.Nif.close(state.handle) do
      :ok -> {:reply, :ok, %{state | open?: false}}
      err -> {:reply, err, state}
    end
  end

  def handle_call({:set_freq, freq_hz}, _from, state),
    do: {:reply, Hamlib.Nif.set_freq(state.handle, freq_hz), state}

  def handle_call(:get_freq, _from, state),
    do: {:reply, Hamlib.Nif.get_freq(state.handle), state}

  def handle_call({:set_mode, mode, pb}, _from, state),
    do: {:reply, Hamlib.Nif.set_mode(state.handle, mode, pb), state}

  def handle_call(:get_mode, _from, state),
    do: {:reply, Hamlib.Nif.get_mode(state.handle), state}

  def handle_call({:set_ptt, on}, _from, state),
    do: {:reply, Hamlib.Nif.set_ptt(state.handle, on), state}

  def handle_call(:get_ptt, _from, state),
    do: {:reply, Hamlib.Nif.get_ptt(state.handle), state}

  @impl true
  def terminate(_reason, %{handle: handle, open?: open?}) do
    # The handle's Drop also closes+cleans up, but be explicit and prompt so the
    # transmitter is de-keyed and the port released immediately on shutdown.
    if open?, do: Hamlib.Nif.close(handle)
    :ok
  end

  def terminate(_reason, _state), do: :ok

  # ── Helpers ──────────────────────────────────────────────────────────────

  defp apply_conf(handle, conf) do
    Enum.reduce_while(conf, :ok, fn {token, value}, :ok ->
      case Hamlib.Nif.set_conf(handle, to_string(token), to_string(value)) do
        :ok -> {:cont, :ok}
        err -> {:halt, err}
      end
    end)
  end

  defp maybe_open(_handle, false), do: :ok
  defp maybe_open(handle, true), do: Hamlib.Nif.open(handle)
end
