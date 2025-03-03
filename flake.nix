{
  # TODO ? flake examples : overlay and package ?
  inputs.nixpkgs.url = "github:nixos/nixpkgs/nixos-24.11";
  inputs.utils.url = "github:numtide/flake-utils";

  outputs = {
    self,
    nixpkgs,
    utils,
  }:
    utils.lib.eachDefaultSystem (
      system: let
        pkgs = import nixpkgs {inherit system;};

        version = "0.0.0"; # TODO
        nativeBuildInputs = [
            pkgs.cmake
            pkgs.extra-cmake-modules
            pkgs.libffi
            pkgs.pkg-config
          ];
        buildInputs = [
            pkgs.luajit_2_1
            pkgs.wayland
            pkgs.wayland-scanner
            pkgs.xorg.libX11
          ];
        neoclip-lib = pkgs.stdenv.mkDerivation {
          pname = "neoclip-lib";
          inherit version;

          src = ./src;

          inherit nativeBuildInputs;
          inherit buildInputs;

          cmakeFlags = [];

          buildPhase = ''
            cmake -S $src -B build
            cmake --build build
          '';

          installPhase = ''
            cmake --install build --strip --prefix $out
          '';

          meta = with pkgs.lib; {
            homepage = "https://github.com/matveyt/neoclip";
            description = "Multi-platform clipboard provider for neovim w/o extra dependencies";
            license = licenses.unlicense;
            platforms = platforms.all;
            maintainers = ["slava.istomin@tuta.io"];
          };
        };
        neoclip-lua-only = pkgs.vimUtils.buildVimPlugin {
          pname = "neoclip";
          inherit version;

          src = ./.;

          unpackPhase = ''
            cp -r $src/{lua,doc} .
          '';
        };
        neoclip = neoclip-lua-only.overrideAttrs {
          dependencies = [ neoclip-lib ];
        };
        shell = pkgs.mkShell {
          buildInputs = nativeBuildInputs ++ buildInputs;
          LD_LIBRARY_PATH = pkgs.lib.makeLibraryPath buildInputs;
        };
      in {
        packages.default = neoclip;
        devShells.default = shell;
      }
    )
    // {
      overlays.default = final: prev: {
        vimPlugins.neoclip = self.outputs.packages."${final.system}".default;
      };
    };
}
