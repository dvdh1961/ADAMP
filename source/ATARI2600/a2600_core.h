#ifndef A2600_CORE_H
#define A2600_CORE_H

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

class Atari2600Core;

class Atari6507
{
public:
    explicit Atari6507(Atari2600Core& bus);
    void reset();
    void startAt(uint16_t address);
    int step();

    uint16_t pc() const { return m_pc; }
    uint8_t a() const { return m_a; }
    uint8_t x() const { return m_x; }
    uint8_t y() const { return m_y; }
    uint8_t sp() const { return m_sp; }
    uint8_t status() const { return m_p; }

private:
    enum Flag : uint8_t { C=0x01, Z=0x02, I=0x04, D=0x08, B=0x10, U=0x20, V=0x40, N=0x80 };
    Atari2600Core& m_bus;
    uint16_t m_pc = 0;
    uint8_t m_a = 0, m_x = 0, m_y = 0, m_sp = 0xFD, m_p = U | I;
    int m_busCycles = 0;

    uint8_t read(uint16_t address);
    void write(uint16_t address, uint8_t value);
    uint8_t fetch();
    uint16_t fetch16();
    uint16_t read16(uint16_t address);
    uint16_t read16bug(uint16_t address);
    void push(uint8_t value);
    uint8_t pull();
    void setNZ(uint8_t value);
    void setFlag(Flag flag, bool set);
    bool flag(Flag flag) const;
    void adc(uint8_t value);
    void sbc(uint8_t value);
    void compare(uint8_t reg, uint8_t value);
    uint8_t asl(uint8_t value);
    uint8_t lsr(uint8_t value);
    uint8_t rol(uint8_t value);
    uint8_t ror(uint8_t value);
    int branch(bool condition);
};

class Atari2600Core
{
public:
    struct DebugTraceEntry {
        uint16_t pc=0;
        uint8_t opcode=0, operand1=0, operand2=0;
        uint8_t a=0, x=0, y=0, sp=0, status=0;
    };
    enum class Mapper { Auto, Cart2K, Cart4K, F8, F6, F6SC, F4, FE, E0, E7, ThreeF, AR };

    static constexpr int Width = 160;
    // Stella keeps a 160x240 viewable TIA buffer.  The 228 value used before
    // is the number of colour clocks per scanline, not the visible height.
    static constexpr int Height = 240;
    static constexpr int SampleRate = 44100;
    static const char* coreRevision();

    Atari2600Core();
    bool loadRom(const std::string& fileName, std::string* error = nullptr);
    bool loadRomData(const std::vector<uint8_t>& bytes, Mapper mapper = Mapper::Auto,
                     std::string* error = nullptr);
    void eject();
    void reset();
    bool runFrame(std::vector<int16_t>& monoAudio,
                  const std::vector<uint16_t>& breakpoints = {});
    void debugStepInstruction();
    uint8_t debugPeek(uint16_t address) const;

    uint8_t read(uint16_t address);
    void write(uint16_t address, uint8_t value);
    void tick(int cpuCycles);

    void setJoystick(int port, bool up, bool down, bool left, bool right, bool fire);
    void setPaddle(int paddle, int position, bool fire);
    void setConsoleSwitches(bool resetPressed, bool selectPressed, bool color,
                            bool leftDifficultyA, bool rightDifficultyA);
    void setResetSwitch(bool pressed);
    void setNtsc(bool ntsc);
    void setPhosphorEnabled(bool enabled);

    const uint32_t* frameBuffer() const { return m_frame.data(); }
    bool hasRom() const { return !m_rom.empty(); }
    Mapper mapper() const { return m_mapper; }
    const char* mapperName() const;
    const Atari6507& cpu() const { return m_cpu; }
    int debugScanline() const { return m_tia.scanline; }
    int lastFrameScanlines() const { return m_lastFrameScanlines; }
    int debugColorClock() const { return m_tia.colorClock; }
    uint8_t debugSwcha() const { return m_riot.swcha; }
    uint8_t debugSwchb() const { return m_riot.swchb; }
    uint8_t debugIntim() const { return m_riot.intim; }
    uint8_t debugInstat() const { return m_riot.instat; }
    int debugBank() const { return m_bank; }
    std::size_t debugArBlock() const { return m_arCurrentBlock; }
    std::vector<DebugTraceEntry> debugTrace() const;
    void setDebugTraceEnabled(bool enabled);

private:
    friend class Atari6507;

    struct Riot {
        std::array<uint8_t, 128> ram{};
        uint8_t swcha = 0xFF;
        uint8_t swchaOut = 0;
        uint8_t swchaDdr = 0;
        uint8_t swchb = 0xCB;
        uint8_t intim = 0;
        uint8_t instat = 0;
        int prescaler = 1;
        int divider = 1;
        bool underflow = false;
        int zeroReadWindow = 0;
        void reset();
        void tick(int cycles);
        void setTimer(uint8_t value, int scale);
    } m_riot;

    struct Tia {
        struct DelayedWrite {
            uint8_t address = 0;
            uint8_t value = 0;
            int clocks = 0;
            bool active = false;
        };
        std::array<uint8_t, 64> reg{};
        uint8_t input[6]{0x80,0x80,0x80,0x80,0x80,0x80};
        bool paddleEnabled[4]{false,false,false,false};
        int paddleThreshold[4]{0,0,0,0};
        int paddleChargeClocks = 0;
        std::array<DelayedWrite, 16> delayedWrites{};
        int colorClock = 0;
        int scanline = 0;
        bool frameDone = false;
        bool wsync = false;
        bool vsync = false;
        bool vblank = true;
        int vsyncStartLine = 0;
        int visibleStartLine = 0;
        uint8_t collision[8]{};
        uint8_t grpNew[2]{};
        uint8_t grpOld[2]{};
        uint8_t playerCounter[2]{};
        int8_t playerRenderCounter[2]{};
        uint8_t playerSampleCounter[2]{};
        uint8_t playerCopy[2]{};
        uint8_t playerDivider[2]{1,1};
        uint8_t playerDividerPending[2]{1,1};
        int8_t playerDividerChangeCounter[2]{-1,-1};
        uint8_t playerMode[2]{};
        bool playerRendering[2]{};
        bool playerOn[2]{};
        uint8_t missileCounter[2]{};
        int8_t missileRenderCounter[2]{};
        bool missileRendering[2]{};
        bool missileOn[2]{};
        uint8_t ballCounter{};
        int8_t ballRenderCounter{-4};
        bool ballRendering{};
        bool ballOn{};
        bool objectMoving[5]{};
        uint8_t movementClock{};
        bool movementInProgress{};
        bool extendedHblank{};
        bool playfieldOn{};
        bool playfieldReflected{};
        bool ballEnableNew = false;
        bool ballEnableOld = false;
        int p0x = 20, p1x = 100, m0x = 20, m1x = 100, ballx = 80;
        bool audClockEnable[2]{};
        bool audNoiseFeedback[2]{};
        bool audNoiseBit[2]{};
        bool audPulseHold[2]{};
        uint8_t audDivider[2]{};
        uint8_t audPulse[2]{};
        uint8_t audNoise[2]{};
        double sampleAccumulator = 0.0;
        double audioDcInput = 0.0;
        double audioDcOutput = 0.0;
        std::vector<int16_t>* audio = nullptr;
        void reset();
    } m_tia;

    Atari6507 m_cpu;
    std::vector<uint8_t> m_rom;
    Mapper m_mapper = Mapper::Cart4K;
    int m_bank = 0;
    std::array<uint8_t, 128> m_superchipRam{};
    std::array<int, 4> m_e0Banks{{0,1,2,7}};
    std::array<uint8_t, 8192> m_arMemory{};
    std::array<std::size_t, 2> m_arOffsets{{4096,6144}};
    uint8_t m_arConfiguration = 0;
    uint8_t m_arDataHold = 0;
    uint64_t m_busAccessCount = 0;
    uint64_t m_arDataHoldAccess = 0;
    bool m_arWriteEnabled = false;
    bool m_arWritePending = false;
    std::size_t m_arCurrentBlock = 0;
    std::array<uint32_t, Width * Height> m_frame{};
    std::array<uint32_t, Width * Height> m_previousDisplayFrame{};
    bool m_phosphorEnabled = false;
    bool m_havePreviousDisplayFrame = false;
    std::array<uint32_t, 128> m_palette{};
    bool m_ntsc = true;
    bool m_forceNtscPalette = false;
    bool m_mindLinkEnabled = false;
    bool m_mindLinkLeft = false;
    bool m_mindLinkRight = false;
    bool m_mindLinkFire = false;
    uint16_t m_mindLinkPosition = 0x2A00;
    uint8_t m_mindLinkBit[2]{0,0};
    int m_totalScanlines = 262;
    int m_lastFrameScanlines = 0;
    bool m_resetPressed = false;
    bool m_selectPressed = false;
    bool m_color = true;
    bool m_leftDifficultyA = false;
    bool m_rightDifficultyA = false;
    bool m_cpuStepping = false;
    bool m_tiaWritePending = false;
    uint8_t m_pendingTiaAddress = 0;
    uint8_t m_pendingTiaValue = 0;
    std::array<DebugTraceEntry, 256> m_debugTrace{};
    std::size_t m_debugTraceHead = 0;
    std::size_t m_debugTraceCount = 0;
    bool m_debugTraceEnabled = false;
    void recordDebugTrace();

    Mapper detectMapper(const std::vector<uint8_t>& data) const;
    uint8_t readCartridge(uint16_t address);
    void cartridgeHotspot(uint16_t address);
    bool arLoadBlock(std::size_t block);
    bool arActivateBlock(std::size_t block);
    void arBankConfiguration(uint8_t configuration);
    void arHandleAccess(uint16_t address);
    std::size_t arImageIndex(uint16_t address) const;
    void tiaWrite(uint8_t address, uint8_t value);
    void applyTiaWrite(uint8_t address, uint8_t value);
    uint8_t tiaRead(uint8_t address);
    uint8_t tiaResxCounter() const;
    void tickTiaColorClock();
    void tickMovement();
    void renderPixel();
    uint8_t playfieldPixel(int x) const;
    void tickPlayfield(int x);
    bool playerPixel(int player, int x) const;
    void setPlayerNusiz(int player, uint8_t value);
    void tickPlayer(int player);
    void tickMissile(int missile);
    bool missilePixel(int missile, int x) const;
    void tickBall();
    bool ballPixel(int x) const;
    void updatePalette();
    void updateInputPorts();
    uint8_t riotReadSwcha() const;
    void riotWriteSwcha(uint8_t value);
    void clockAudioPhase0(int channel);
    void clockAudioPhase1(int channel);
    void generateAudioCycle();
};

#endif
