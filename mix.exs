defmodule HamlibEx.MixProject do
  use Mix.Project

  @version "0.1.0"
  @source_url "https://github.com/HeroesLament/hamlib_ex"

  def project do
    [
      app: :hamlib_ex,
      version: @version,
      elixir: "~> 1.16",
      start_permanent: Mix.env() == :prod,
      deps: deps(),
      description:
        "Elixir/BEAM bindings for Hamlib (the Ham Radio Control Library) via a Rustler NIF.",
      package: package(),
      name: "hamlib_ex",
      source_url: @source_url,
      docs: docs()
    ]
  end

  def application do
    [
      extra_applications: [:logger]
    ]
  end

  defp deps do
    [
      # Pinned to 0.37 to stay in lockstep with the rest of the stack
      # (phy_modem / milwave consumers and the GenericJam Android-RTLD
      # Rust fork are all on 0.37).
      {:rustler, "~> 0.37.0"},
      {:ex_doc, "~> 0.34", only: :dev, runtime: false}
    ]
  end

  defp package do
    [
      licenses: ["LGPL-2.1-or-later"],
      links: %{"GitHub" => @source_url},
      files: ~w(lib
                native/hamlib_nif/src
                native/hamlib_nif/c_src/hamlib_shim.c
                native/hamlib_nif/c_src/hamlib_shim.h
                native/hamlib_nif/build.rs
                native/hamlib_nif/Cargo.toml
                native/hamlib_nif/Cargo.lock
                mix.exs README.md LICENSE)
    ]
  end

  defp docs do
    [
      main: "readme",
      extras: ["README.md"],
      source_ref: "v#{@version}"
    ]
  end
end
