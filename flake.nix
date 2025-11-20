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
        pkgs.gcc14Stdenv.mkDerivation {
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
  };
}
