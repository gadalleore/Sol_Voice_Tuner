// Standalone: verifies root labels and F# Major scale snapping. Build: SolVoiceTuner_KeyDumpConsole.
#include <cmath>
#include <iostream>

#include "ScaleQuantizer.h"

int main()
{
    std::cout << "[SolVoiceTuner][KeyDumpConsole] SolTune::rootChoiceLabel\n";
    for (int i = 0; i < 12; ++i)
        std::cout << "[SolVoiceTuner][KeyDumpConsole]   " << i << "    \"" << SolTune::rootChoiceLabel (i)
                  << "\"\n";

    const auto fsMajor = SolTune::allowedPitchClasses (SolTune::Root::Fs, SolTune::Scale::Major);
    std::cout << "[SolVoiceTuner][KeyDumpConsole] F# Major pitch classes:";
    for (int pc = 0; pc < 12; ++pc)
        if (fsMajor & (1u << pc))
            std::cout << " " << SolTune::rootName (pc);
    std::cout << "\n";

    const float fSharpHz = SolTune::midiToHz (66.0f);
    const float snapped  = SolTune::snapHzToScale (fSharpHz, SolTune::Root::Fs, SolTune::Scale::Major);
    const int midi       = (int) std::lround (SolTune::hzToMidi (snapped));
    std::cout << "[SolVoiceTuner][KeyDumpConsole] F#4 snap -> " << SolTune::midiNoteName (midi)
              << " (midi " << midi << ")\n";

    const int rootStored = SolTune::clampChoiceIndex (6, 12);
    const auto asB       = SolTune::allowedPitchClasses ((SolTune::Root) 11, SolTune::Scale::Major);
    if (rootStored != 6 || midi != 66 || (fsMajor == asB))
    {
        std::cout << "[SolVoiceTuner][KeyDumpConsole] FAILED scale/key sanity checks\n";
        return 1;
    }

    std::cout << "[SolVoiceTuner][KeyDumpConsole] OK\n";
    return 0;
}
