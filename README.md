# Groovebox (JUCE-skelett)

Bitwig-inspirerad clip launcher med groove per spår och ljudutgång via ASIO.

## Bygga på Windows
1. Installera Visual Studio 2022 (arbetsbelastningen "Skrivbordsutveckling med C++") och CMake.
2. Ladda ner Steinbergs ASIO SDK (kräver gratis utvecklarkonto och licensavtal) och packa upp.
3. I en terminal i den här mappen:

       cmake -B build -DASIO_SDK_DIR="C:/sökväg/till/ASIOSDK"
       cmake --build build --config Release

4. Starta `build/Groovebox_artefacts/Release/Groovebox.exe`.
5. Klicka på "Ljudinställningar" och välj din ASIO-driver.

Utan ASIO_SDK_DIR byggs programmet ändå och använder Windows egna ljudgränssnitt.

## Licens
JUCE har egna licensvillkor (AGPLv3 eller kommersiell). ASIO är ett varumärke
som tillhör Steinberg Media Technologies GmbH.

## Bygga utan att installera något lokalt
Ladda upp mappen till ett GitHub-repo. Fliken "Actions" bygger då Groovebox.exe
automatiskt (utan ASIO). Ladda ner filen under "Artifacts" på den färdiga körningen.

## ASIO i det automatiska bygget
1. Ladda ner ASIO SDK och kopiera in den i mappen `asio-sdk/` (se LÄS-MIG.txt där).
2. Ladda upp till ett **publikt** GitHub-repo. Actions hittar SDK:n själv och bygger med ASIO.
3. Släpp projektet under GPLv3/AGPLv3 om du använder SDK:ns GPLv3-licens.

## VST3-plugin
- Tryck **Sök VST3** en gång (söker i standardmapparna, sparar listan).
- Varje spår har en plugin-knapp. Välj ett **instrument** för att ersätta den inbyggda synten
  (spårets mönster skickas som MIDI-noter), eller en **effekt** för att köra spårets ljud genom den.
- Pluginfönstret öppnas direkt efter laddning, och via menyn "Visa pluginfönster".
- Sökningen sker i samma process, så en trasig plugin kan krascha sökningen. Starta om och
  sök igen, så hoppas den över.
- Plugin-inställningar sparas inte i projektet än.
