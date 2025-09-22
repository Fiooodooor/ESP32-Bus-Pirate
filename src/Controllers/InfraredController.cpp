#include "InfraredController.h"

/*
Constructor
*/
InfraredController::InfraredController(
    ITerminalView&           view,
    IInput&                  terminalInput,
    InfraredService&         service,
    LittleFsService&         littleFsService,
    ArgTransformer&          argTransformer,
    InfraredRemoteTransformer& infraredRemoteTransformer,
    UserInputManager&        userInputManager,
    UniversalRemoteShell&    universalRemoteShell
)
    : terminalView(view),
      terminalInput(terminalInput),
      infraredService(service),
      littleFsService(littleFsService),
      argTransformer(argTransformer),
      infraredRemoteTransformer(infraredRemoteTransformer),
      userInputManager(userInputManager),
      universalRemoteShell(universalRemoteShell)
{}

/*
Entry point to handle Infrared command
*/
void InfraredController::handleCommand(const TerminalCommand& command) {
    if (command.getRoot() == "config")            handleConfig();
    else if (command.getRoot() == "send")         handleSend(command);
    else if (command.getRoot() == "receive")      handleReceive();
    else if (command.getRoot() == "devicebgone")  handleDeviceBgone();
    else if (command.getRoot() == "remote")       handleRemote();
    else if (command.getRoot() == "replay")       handleReplay(command);
    else if (command.getRoot() == "load")         handleLoad(command);
    else if (command.getRoot() == "setprotocol")  handleSetProtocol();
    else handleHelp();
}

/*
Send
*/
void InfraredController::handleSend(const TerminalCommand& command) {
    std::istringstream iss(command.getArgs());
    std::string subStr, cmdStr;
    iss >> subStr >> cmdStr;
    auto addrStr = command.getSubcommand();

    if (addrStr.empty() || subStr.empty() || cmdStr.empty()) {
        terminalView.println("Missing argument. Usage: send <device> <sub> <cmd>");
        return;
    }

    int device, subdevice, function;
    if (!argTransformer.parseInt(addrStr, device) ||
        !argTransformer.parseInt(subStr, subdevice) ||
        !argTransformer.parseInt(cmdStr, function)) {
        terminalView.println("Invalid number format. Use decimal or hex.");
        return;
    }

    InfraredCommand infraredCommand;
    infraredCommand.setDevice(device);
    infraredCommand.setSubdevice(subdevice);
    infraredCommand.setFunction(function);
    infraredCommand.setProtocol(state.getInfraredProtocol());

    for (int i = 0; i < 3; ++i) {
        infraredService.sendInfraredCommand(infraredCommand);
        delay(100);
    }

    terminalView.println("IR command sent with protocol " + InfraredProtocolMapper::toString(state.getInfraredProtocol()));
}

/*
Receive
*/
void InfraredController::handleReceive() {
    terminalView.println("Receiving infrared signal...Press [ENTER] to cancel.");
    terminalView.println("");
    
    infraredService.startReceiver();
    while (true) {
        // Check Enter press
        char c = terminalInput.readChar();
        if (c == '\r' || c == '\n') {
            terminalView.println("INFRARED Receive: Cancelled by user.");
            return;
        }

        // Try receive and decode signal
        InfraredCommand cmd = infraredService.receiveInfraredCommand();
        if (cmd.getProtocol() != RAW) {
            terminalView.println("");
            terminalView.println("Infrared signal received:");
            terminalView.println("  Protocol : " + InfraredProtocolMapper::toString(cmd.getProtocol()));
            terminalView.println("  Device   : " + std::to_string(cmd.getDevice()));
            terminalView.println("  SubDev   : " + std::to_string(cmd.getSubdevice()));
            terminalView.println("  Command  : " + std::to_string(cmd.getFunction()));
            terminalView.println("");
            terminalView.println("INFRARED Receive: Waiting for next signal or press [ENTER] to exit.");
        }
    }
    infraredService.stopReceiver();
}

/* 
DeviceBgone
*/
void InfraredController::handleDeviceBgone() {
    terminalView.println("Sending Device-B-Gone commands... Press [ENTER] to stop");

    for (const auto& cmdStruct : universalOnOff) {

        // Convert to InfraredCommand model
        auto cmd = InfraredCommand(
            cmdStruct.proto,
            cmdStruct.device,
            cmdStruct.subdevice,
            cmdStruct.function
        );
        
        char c = terminalInput.readChar();
        if (c == '\r' || c == '\n') {
            terminalView.println("Infrared Device-B-Gone: Interrupted by user.");
            return;
        }

        for (int i = 0; i < 2; ++i) { // send 2x per command
            infraredService.sendInfraredCommand(cmd);
            delay(100);
        }

        terminalView.println(
            "Sent On/Off to protocol=" + InfraredProtocolMapper::toString(cmd.getProtocol()) +
            " device=" + std::to_string(cmd.getDevice()) +
            " sub=" + std::to_string(cmd.getSubdevice()) +
            " cmd=" + std::to_string(cmd.getFunction())
        );
    }

    terminalView.println("Device-B-Gone sequence completed.");
}

/*
Universal Remote
*/
void InfraredController::handleRemote() {
    universalRemoteShell.run();
}

/*
Replay
*/
void InfraredController::handleReplay(const TerminalCommand& command) {

    // Optional replay count
    uint32_t replayCount = 0; // 0 = infinite
    const std::string sub = command.getSubcommand();
    if (!sub.empty() && argTransformer.isValidNumber(sub)) {
        replayCount = argTransformer.toUint32(sub);
    }

    // Record frames
    std::vector<IRFrame> tape;
    if (!recordFrames(tape)) {
        return; // No frames captured
    }

    // Playback frames
    playbackFrames(tape, replayCount);
}

bool InfraredController::recordFrames(std::vector<IRFrame>& tape) {
    tape.clear();
    tape.reserve(MAX_IR_FRAMES);

    terminalView.println("INFRARED Replay: Recording raw IR frames (max 64)... Press [ENTER] to stop.\n");

    // Start the capture
    infraredService.startReceiver();
    uint32_t lastMillis = millis();
    while (true) {
        // Stop if Enter pressed
        char c = terminalInput.readChar();
        if (c == '\r' || c == '\n') break;

        // Max frames reached
        if (tape.size() >= MAX_IR_FRAMES) {
            terminalView.println("\nINFRARED Replay: Reached maximum of 64 frames, stopping recording...\n");
            break;
        }

        // Attempt to capture
        std::vector<uint16_t> timings;
        uint32_t khz = 0;
        if (infraredService.receiveRaw(timings, khz)) {
            const uint32_t now = millis();
            const uint32_t gap = tape.empty() ? 0u : (now - lastMillis);
            lastMillis = now;

            tape.push_back(IRFrame{ std::move(timings), khz, gap });
            terminalView.println(
                "  📥 Captured frame #" + std::to_string(tape.size()) +
                " (gap " + std::to_string(gap) + " ms, carrier " + std::to_string(khz) + " kHz)"
            );
        }
    }
    infraredService.stopReceiver();

    // Nothing
    if (tape.empty()) {
        terminalView.println("INFRARED Replay: No frames captured. Nothing to replay.");
        return false;
    }

    return true;
}

void InfraredController::playbackFrames(const std::vector<IRFrame>& tape, uint32_t replayCount) {
    if (replayCount == 0) {
        terminalView.println("\nINFRARED Replay: Playing back with original delays. Press [ENTER] to stop.\n");
    } else {
        terminalView.println("\nINFRARED Replay: Playing back " + std::to_string(replayCount) +
                             " time(s) with original delays. Press [ENTER] to stop.\n");
    }

    // Loop through the frames and send them
    uint32_t playedLoops = 0;
    while (true) {
        if (replayCount > 0 && playedLoops >= replayCount) break;

        for (size_t i = 0; i < tape.size(); ++i) {
            const auto& f = tape[i];

            // Check for Enter press and wait for gap
            uint32_t start = millis();
            while (millis() - start < f.gapMs) {
                char c = terminalInput.readChar();
                if (c == '\r' || c == '\n') {
                    terminalView.println("\nINFRARED Replay: Stopped by user.");
                    return;
                }
                delay(1);
            }

            // Log and send frame
            terminalView.println(
                "  📤 Sending frame #" + std::to_string(i) +
                " (gap " + std::to_string(f.gapMs) + " ms, carrier " + std::to_string(f.khz) + " kHz)"
            );
            infraredService.sendRaw(f.timings, f.khz);
        }
        ++playedLoops;
    }

    terminalView.println("\nINFRARED Replay: Completed (" + std::to_string(playedLoops) + " loop(s)).");
}

/*
Set protocol
*/
void InfraredController::handleSetProtocol() {
    terminalView.println("");
    terminalView.println("Select Infrared Protocol:");

    std::vector<InfraredProtocolEnum> protocols;

    for (int i = 0; i <= static_cast<int>(RAW); ++i) {
        InfraredProtocolEnum proto = static_cast<InfraredProtocolEnum>(i);
        std::string name = InfraredProtocolMapper::toString(proto);

        // avoid double name
        if (!name.empty() && 
            std::find_if(protocols.begin(), protocols.end(),
                [proto](InfraredProtocolEnum e) { return InfraredProtocolMapper::toString(e) == InfraredProtocolMapper::toString(proto); }) == protocols.end()) {
            protocols.push_back(proto);
            terminalView.println("  " + std::to_string(protocols.size()) + ". " + name);
        }
    }

    terminalView.println("");
    terminalView.print("Protocol Number > ");

    std::string inputStr;
    while (true) {
        char c = terminalInput.handler();
        if (c == '\r' || c == '\n') {
            terminalView.println("");
            break;
        }

        if (c == CARDPUTER_SPECIAL_ARROW_DOWN || 
            c == CARDPUTER_SPECIAL_ARROW_UP) {
            terminalView.print(std::string(1, c));
            continue;
        }        

        if (std::isdigit(c)) {
            if (c != CARDPUTER_SPECIAL_ARROW_DOWN || c != CARDPUTER_SPECIAL_ARROW_UP) {
                inputStr += c;
            }
            terminalView.print(std::string(1, c));
        } else {
            terminalView.println("\nInvalid input: only digits allowed.");
            return;
        }
    }

    if (inputStr.empty()) {
        terminalView.println("No input given.");
        return;
    }

    int index = std::stoi(inputStr);
    if (index >= 1 && index <= static_cast<int>(protocols.size())) {
        InfraredProtocolEnum selected = protocols[index - 1];
        GlobalState::getInstance().setInfraredProtocol(selected);
        terminalView.println("Protocol changed to " + InfraredProtocolMapper::toString(selected));
    } else {
        terminalView.println("Invalid protocol number.");
    }
}

/*
Load
*/
void InfraredController::handleLoad(TerminalCommand const& command) {
    if (!littleFsService.mounted()) {
        littleFsService.begin();
        return;
    }

    // Get IR files from LittleFS
    auto files = littleFsService.listFiles(/*root*/ "/", ".ir");
    if (files.empty()) {
        terminalView.println("INFRARED: No .ir files found in LittleFS root ('/').");
        return;
    }

    // Select file
    terminalView.println("\n=== '.ir' files in LittleFS ===");
    uint16_t idxFile = userInputManager.readValidatedChoiceIndex("File number", files, 0);
    const std::string& chosen = files[idxFile];

    // Check size
    int MAX_FILE_SIZE = 32 * 1024; // 32 KB
    auto fileSize = littleFsService.getFileSize("/" + chosen);
    if (fileSize == 0 || fileSize > MAX_FILE_SIZE) {
        terminalView.println("\nINFRARED: File size invalid (>32KB): " + chosen);
        return;
    }

    // Load file content
    std::string text;
    if (!littleFsService.readAll("/" + chosen, text)) {
        terminalView.println("\nINFRARED: Failed to read file: " + chosen);
        return;
    }

    // Verify format
    if (!infraredRemoteTransformer.isValidInfraredFile(text)) {
        terminalView.println("\nINFRARED: Unrecognized .ir format or empty: " + chosen);
        return;
    }

    // Extract commands
    auto cmds = infraredRemoteTransformer.transformFromFileFormat(text);
    if (cmds.empty()) {
        terminalView.println("\nINFRARED: No commands found in: " + chosen);
        return;
    }

    // Cmds names
    auto cmdStrings = infraredRemoteTransformer.extractFunctionNames(cmds);
    cmdStrings.push_back("Exit File"); // for exit option

    while (true) {
        // Select command
        terminalView.println("\n=== Commands in file '" + chosen + "' ===");
        uint8_t idxCmd = userInputManager.readValidatedChoiceIndex("Command number", cmdStrings, 0);
        if (idxCmd == cmdStrings.size()-1) {
            terminalView.println("Exiting command send...\n");
            break;
        }

        // Send
        infraredService.sendInfraredFileCommand(cmds[idxCmd]);
        terminalView.println("\n ✅  Sent command '" + cmds[idxCmd].functionName + "' from file '" + chosen + "'");
    }
}

/*
Config
*/
void InfraredController::handleConfig() {
    terminalView.println("\nInfrared Configuration:");

    const auto& forbidden = state.getProtectedPins();

    uint8_t txPin = userInputManager.readValidatedPinNumber("Infrared TX pin", state.getInfraredTxPin(), forbidden);
    uint8_t rxPin = userInputManager.readValidatedPinNumber("Infrared RX pin", state.getInfraredRxPin(), forbidden);

    state.setInfraredTxPin(txPin);
    state.setInfraredRxPin(rxPin);
    infraredService.configure(txPin, rxPin);

    // Protocol
    auto selectedProtocol = InfraredProtocolMapper::toString(state.getInfraredProtocol());
    terminalView.println("Current protocol: '" + selectedProtocol + "'");
    terminalView.println("You can change it with 'setprotocol'");

    terminalView.println("Infrared configured.\n");
}

/*
Help
*/
void InfraredController::handleHelp() {
    terminalView.println("Unknown INFRARED command. Usage:");
    terminalView.println("  send <addr> <subadd> <cmd>");
    terminalView.println("  receive");
    terminalView.println("  setprotocol");
    terminalView.println("  devicebgone");
    terminalView.println("  remote");
    terminalView.println("  replay");
    terminalView.println("  load");
    terminalView.println("  config");
}

void InfraredController::ensureConfigured() {
    if (!configured) {
        handleConfig();
        configured = true;
        return;
    }

    // Always reconfigure before use
    uint8_t tx = state.getInfraredTxPin();
    uint8_t rx = state.getInfraredRxPin();
    infraredService.configure(tx, rx);
}