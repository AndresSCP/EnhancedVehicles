# Enhanced Vehicles — credits and distribution review

Working review, 14 October 2026. This is a component inventory and release checklist, not a blanket permission for every third-party file.

| Component | Credit | Use and distribution plan |
|---|---|---|
| Starfield and official vehicle content | Bethesda Game Studios | Reference installed game forms and animations; do not package extracted game archives, executable code or stock meshes/textures. Check applicable Creation Kit/distribution terms before release. |
| SFSE runtime | Ian Patterson and the SFSE team | External requirement. Do not bundle its loader/runtime. |
| SFSE PluginAPI.h | Ian Patterson, copyright 2023 | Header used to build our DLL; MIT permission specifically covers this header. Include docs/licenses/SFSE-PluginAPI-LICENSE.txt in distributed source/binary notices. The rest of the SFSE repository is not under that permission. |
| MinHook and its bundled disassembler portions | Tsuda Kageyu; additional authors in its full license | Compiled into our DLL. Include the complete docs/licenses/MinHook-LICENSE.txt, including its subsidiary notices. |
| Cassiopeia Papyrus Extender | LarannKiar | External requirement, function calls from our own scripts. Do not bundle its DLL, PSC or PEX, modify/reupload its files, or treat its assets as permissively licensed. Nexus lists restrictions on modification, asset use, sales and Donation Points. Calling an external installed API is distinct from copying its assets; obtain clarification for any proposed asset reuse or monetized distribution. |
| OSF UI and OSF Settings | ozooma10 | Optional external UI dependencies. Our current integration uses native Gameplay Options already enumerated by OSF; no framework source, DLL or shared web assets are incorporated into our package. Projects use GPL-3.0 with additional exceptions; review those exact terms if future integration copies or links covered code. |

No source/assets from Command Followers, The Gang's All Here or Real Fuel are currently required or incorporated in the runtime package. Their study or testing can be acknowledged as inspiration separately, without suggesting ownership or dependency. CommonLibSF is not currently linked into our native bridge; do not list it as bundled simply because other dependencies use it.

Official sources:
- SFSE header license: https://github.com/ianpatt/sfse/blob/master/LICENSE
- SFSE distribution/readme: https://github.com/ianpatt/sfse/blob/master/sfse_readme.txt
- CPE permissions/author: https://www.nexusmods.com/starfield/mods/10896?tab=description
- OSF UI license and exceptions: https://github.com/ozooma10/osf-ui
- OSF Settings license and exceptions: https://github.com/ozooma10/osf-settings

Release packaging must contain only our own plugins/scripts/bridge, compatibility overrides as permitted, and required license notices. Analysis libraries and extracted research assets in work/ are development material, not release contents. Project-authored source is distributed under the repository's MIT license. Third-party components retain their own notices and terms.