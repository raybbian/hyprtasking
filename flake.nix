{
  description = "Hyprtasking";

  inputs = {
    hyprland.url = "github:hyprwm/Hyprland";
    nixpkgs.follows = "hyprland/nixpkgs";
    systems.url = "github:nix-systems/default-linux";
  };

  outputs = {
    self,
    nixpkgs,
    hyprland,
    systems,
    ...
  }: let
    inherit (nixpkgs) lib;

    forSystems = attrs:
      lib.genAttrs (import systems) (
        system:
          attrs system nixpkgs.legacyPackages.${system}
      );
  in {
    packages = forSystems (system: pkgs: {
      hyprtasking = let
        hyprlandPkg = hyprland.packages.${system}.hyprland;
      in
        pkgs.stdenv.mkDerivation {
          pname = "hyprtasking";
          version = "0.1";

          src = ./.;

          nativeBuildInputs = [pkgs.meson pkgs.ninja] ++ hyprlandPkg.nativeBuildInputs;
          buildInputs = [hyprlandPkg] ++ hyprlandPkg.buildInputs;

          meta = with lib; {
            homepage = "https://github.com/raybbian/hyprtasking";
            description = "Powerful workspace management plugin, packed with features ";
            license = licenses.bsd3;
            platforms = platforms.linux;
          };
        };

      default = self.packages.${system}.hyprtasking;
    });

    checks = forSystems (system: pkgs: let
      hyprlandPkg = hyprland.packages.${system}.hyprland;
    in {
      hyprland-hook-symbols = pkgs.runCommand "hyprtasking-hyprland-hook-symbols" {
        nativeBuildInputs = [pkgs.binutils];
      } ''
        sed -n 's/.*"\(_ZN[^"]*\)".*/\1/p' ${self}/src/main.cpp > required-symbols
        test -s required-symbols
        nm -D -j ${hyprlandPkg}/bin/.Hyprland-wrapped > available-symbols

        while IFS= read -r symbol; do
          if ! grep -Fx -- "$symbol" available-symbols >/dev/null; then
            echo "Hyprland does not export required hook symbol: $symbol" >&2
            exit 1
          fi
        done < required-symbols

        touch "$out"
      '';
    });
  };
}
