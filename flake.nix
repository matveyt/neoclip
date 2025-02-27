{
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
      in {
        packages.default = pkgs.stdenv.mkDerivation {
          pname = "neoclip";
          version = "0.1.0"; /* TODO */

          src = ./src;

          /* TODO extract? */
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

          cmakeFlags = [];

          buildPhase = ''
            cmake -S $src -B build
            cmake --build build
          '';

          installPhase = ''
            mkdir $out
            strip -s build/*.so
            mv build/*.so $out
          '';

          meta = with pkgs.lib; {
            homepage = "https://github.com/MordragT/nix-templates/tree/master/cpp";
            description = "Multi-platform clipboard provider for neovim w/o extra dependencies";
            licencse = licenses.mit;
            platforms = platforms.all;
            maintainers = ["slava.istomin@tuta.io"];
          };
        };

        devShells.default = pkgs.mkShell rec {
          buildInputs = [
            pkgs.luajit_2_1
            pkgs.cmake
            pkgs.extra-cmake-modules
            pkgs.libffi
            pkgs.pkg-config
            pkgs.xorg.libX11
            pkgs.wayland
            pkgs.wayland-scanner
          ];

          LD_LIBRARY_PATH = pkgs.lib.makeLibraryPath buildInputs;
        };
      }
    )
    // {
      overlays.default = self: pkgs: {
        hello = self.packages."${pkgs.system}".hello;
      };
    };
}
