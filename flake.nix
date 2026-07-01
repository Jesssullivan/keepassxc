{
  description = "Fork-only KeePassXC development shell";

  inputs.nixpkgs.url = "github:NixOS/nixpkgs/nixpkgs-unstable";

  outputs =
    { nixpkgs, ... }:
    let
      systems = [
        "aarch64-darwin"
        "x86_64-darwin"
        "aarch64-linux"
        "x86_64-linux"
      ];
      forAllSystems = nixpkgs.lib.genAttrs systems;
    in
    {
      devShells = forAllSystems (
        system:
        let
          pkgs = import nixpkgs { inherit system; };
        in
        {
          default = pkgs.mkShell {
            packages = with pkgs; [
              botan3
              cmake
              minizip
              ninja
              pcsclite
              pkg-config
              qrencode
              qt6.qtbase
              qt6.qtsvg
              qt6.qttools
              readline
              zlib
            ];

            shellHook = ''
              export CMAKE_GENERATOR=Ninja
              export CMAKE_PREFIX_PATH="${pkgs.qt6.qtbase}:${pkgs.qt6.qtsvg}:${pkgs.qt6.qttools}"
              echo "KeePassXC fork-only dev shell. Run scripts/fork-only/install-local-guards.sh once per checkout."
            '';
          };
        }
      );
    };
}
