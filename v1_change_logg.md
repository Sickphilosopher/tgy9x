## HobbyKing Turnigy 9XR v1.1 change log ##
This page contains a description of the additions and changes made by HobbyKing from er9x 762 and tgy9x 1.1
It would have been easier if they published this list by themselves, but they have chosen not to.


| **Files** | **Change** | **Type** |
|:----------|:-----------|:---------|
| tst.xcf s9xsplash.lbm s9xsplash.xbm | Changed start screen text from er9x to Turnigy 9XR | Cosmetic |
| drivers.cpp templates.h | No functional change at all | --- |
| er9x.cpp | Changed abbreviation of Volts from v to V | Cosmetic |
| er9x.cpp | Hard coded trim step size to 1 | Reduction |
| er9x.cpp | Hard coded mid channel alert sound to be same as low battery sound | Reduction |
| er9x.cpp | Reduced beep frequency when trying to set trim outside of max limits (125) | Cosmetic |
| er9x.h menus.cpp | Added new page for channel reversal | Addition |
| english.lang templates.cpp | Changed template name from "Simple 4-CH" to "Simple 6-CH" | Cosmetic |
| menus.cpp | Removed version page. | Reduction |
| menus.cpp | Changed name of normal channel reversal state from "---" to "NOR" | Cosmetic |
| menus.cpp | Changed default limits | Cosmetic |
| menus.cpp | Added stick mode to template call. This parameter is however never used | --- |
| menus.cpp | Changed default limits | Cosmetic |
| menus.cpp | Removed display of numeric channel values | Reduction |
| pers.cpp | Changed default configuration values for potscroll (-> Disabled ), Beepervalue (-> 2), LightAutoOff (-> Enabled) | Cosmetic |
| pers.cpp | Changed default name of owner from "ME" to "  " | Cosmetic |
| stamp\_er9x.h | This file is automatically created by the build process and contains a date stamp, version information and information of code repository version. HobbyKing has hard-coded the information, which kind of removes the idea | Reduction |
| templates.cpp | Removed "Sticky T-Cut" template  | Reduction |
| templates.cpp | Hard coded all templates to use TAER channel order | Reduction |


The functional types are:

| --- | No functional change at all |
|:----|:----------------------------|
| Addition | Functionality has been added |
| Reduction | Functionality has been remeved |
| Cosmetic | Only visual appearance or temporary values like default settings have been changed |