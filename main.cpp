#include <array>
#include <random>
#include <fstream>
#include <filesystem>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <time.h>

#include "SDL2/SDL.h"

// TODO: change this to probably a real function that doesnt exist in release
#ifdef DEBUG
    #define DEBUG_LOG(fmt, ...) printf("[DBG] " fmt "\n", ##__VA_ARGS__)
#else
    #define DEBUG_LOG(fmt, ...)
#endif

struct sdl_t
{
	SDL_Window* window;
};

namespace Config
{
	const char* title = "CHIP8";
	const char* ssDir = "screenshots";
    const char* ssPrefix = "Screenshot_CHIP-8";
	constexpr uint32_t bgColor = 0x00073ea6;
	constexpr uint32_t fgColor = 0x00098fe8;
	constexpr int scaleFac = 18;
	int normalClockSpeed = 601;
	float maxClockSpeedMp = 3.0f;
	float minClockSpeedMp = 0.25f;
	char* romPath{};
	[[maybe_unused]] const char* beepSoundPath{"beep.wav"};
	bool useBeepSound = false;	// uhh why it cant be maybe_unused?
	constexpr uint32_t beepIconColor = 0x00EECC00;
}

namespace Global
{
	int clockSpeed = Config::normalClockSpeed;
}

namespace Random
{
	std::mt19937 mt{std::random_device{}()};
	int get(int min, int max)
	{
		return std::uniform_int_distribution{min, max}(mt);
	}
}

class Chip8
{
private:
	const static int m_scrWidth = 64;
	const static int m_scrHeight = 32;
	std::array<uint8_t, 5*16> m_font {
		0xF0, 0x90, 0x90, 0x90, 0xF0, // 0
		0x20, 0x60, 0x20, 0x20, 0x70, // 1
		0xF0, 0x10, 0xF0, 0x80, 0xF0, // 2
		0xF0, 0x10, 0xF0, 0x10, 0xF0, // 3
		0x90, 0x90, 0xF0, 0x10, 0x10, // 4
		0xF0, 0x80, 0xF0, 0x10, 0xF0, // 5
		0xF0, 0x80, 0xF0, 0x90, 0xF0, // 6
		0xF0, 0x10, 0x20, 0x40, 0x40, // 7
		0xF0, 0x90, 0xF0, 0x90, 0xF0, // 8
		0xF0, 0x90, 0xF0, 0x10, 0xF0, // 9
		0xF0, 0x90, 0xF0, 0x90, 0x90, // A
		0xE0, 0x90, 0xE0, 0x90, 0xE0, // B
		0xF0, 0x80, 0x80, 0x80, 0xF0, // C
		0xE0, 0x90, 0x90, 0x90, 0xE0, // D
		0xF0, 0x80, 0xF0, 0x80, 0xF0, // E
		0xF0, 0x80, 0xF0, 0x80, 0x80  // F
	};

	bool m_draw{true};									// Refresh the screen when true
	std::array<bool, m_scrWidth*m_scrHeight> m_display{};// Emulate the original display
	std::array<uint8_t, 4096>* m_ram = new std::array<uint8_t, 4096>{};
	std::array<uint8_t, 16> m_V{};						// Data registers
	std::array<uint32_t, 12> m_stack{};					// Stack memory for up to 12 addresses
	uint32_t* m_pStack{&m_stack[0]};					// Stack pointer
	uint16_t m_opcode{};								// The current opcode
	uint16_t m_PC{0x200};								// Program counter
	uint16_t m_I{};										// Address register
	uint8_t m_delayTimer{};								// Decrements at 60Hz while > 0
	uint8_t m_soundTimer{};								// Decrements and beeps while > 0

public:
	std::array<bool, 16> keypad{};						// The keys are the hex chars

	Chip8()
	{
		memcpy(&(*m_ram)[0x050], m_font.data(), m_font.size());
	}

	~Chip8()
	{
		delete m_ram;
	}
	
	Chip8(Chip8&) = delete;
	Chip8(Chip8&&) = delete;

	int getWidth() const {return m_scrWidth;}
	int getHeight() const {return m_scrHeight;}
	bool isBeeping() const {return m_soundTimer > 0;}
	bool refreshScreen() 
	{
		if (m_draw)
		{
			m_draw = false;
			return true;
		}

		return false;
	}

	void updateTimers()
	{
		if (m_delayTimer > 0)
			m_delayTimer--;
		
		if (m_soundTimer > 0)
			m_soundTimer--;
	}
	
	// Draws from the chip8 display memory to an SDL_Surface
	void drawDisplay(SDL_Surface* surf) const
	{
		SDL_FillRect(surf, 0, Config::bgColor);

		SDL_Rect rect;
		rect.w = 1 * Config::scaleFac;
		rect.h = 1 * Config::scaleFac;

		for (std::size_t i = 0; i < m_display.size(); i++)
		{
			if (m_display[i])
			{
				rect.x = (i % m_scrWidth) * Config::scaleFac;
				rect.y = (i / m_scrWidth) * Config::scaleFac;
				SDL_FillRect(surf, &rect, Config::fgColor);
			}
		}
	}
	
	// Copies the program binaries from the file to the ram at address 0x200
	bool loadProgram(char* fileName)
	{
		if (!fileName)
		{
			SDL_Log("No rom path specified. Chip 8 requires to boot with a rom\n");
			return false;
		}

		std::ifstream file{fileName, std::ios::binary | std::ios::ate};
		if (!file)
		{
			SDL_Log("Could not find the rom. Make sure the file exists and the path is correct\n");
			return false;
		}

		std::streamsize fileSize = file.tellg();
    	file.seekg(0, std::ios::beg);
		
    	std::vector<uint8_t> buffer(fileSize);
    	if (!file.read(reinterpret_cast<char*>(buffer.data()), fileSize)) 
		{
        	SDL_Log("Failed to read file\n");
        	return false;
    	}

		memcpy(&(*m_ram)[0x200], buffer.data(), buffer.size());

		m_PC = 0x200;
		
		file.close();

		return true;
	}
	
	// Emulates one cycle
	void emulateCycle()
	{
		// Fetch opcode and increment PC by 2
		m_opcode = ((*m_ram)[m_PC] << 8) | ((*m_ram)[m_PC + 1]);
		m_PC += 2;

		bool carry = false;
		uint16_t NNN = m_opcode & 0x0FFF;
		uint8_t NN = m_opcode & 0x00FF;
		uint8_t N = m_opcode & 0x000F;
		uint8_t X = (m_opcode >> 8) & 0x0F;
		uint8_t Y = (m_opcode >> 4) & 0x0F;

		// Decode and execute the opcode
		// Check the first 2 bytes
		switch ((m_opcode & 0xF000) >> 12)
		{
		case 0x0:
			// Clear the screen
			if (NN == 0xE0)
			{
				memset(&m_display[0], 0, m_display.size());
				m_draw = true;

				DEBUG_LOG("Cleared the screen");
				break;
			}
			// Return to subroutine NN
			if (NN == 0xEE)
			{
				// CAN I PUT MY BALLS IN YOUR JAWS, "--"?
				m_PC = *--m_pStack;

				DEBUG_LOG("Returned to subroutine 0x%04X", m_PC);
				break;
			}
			// Empty opcode
			if (NNN == 0x000)
			{
				// This is for when trying to use opcode 0x0000 which might be 
				// just the empty ram

				SDL_Log("Tried executing opcode 0x0000 but this might be just the empty ram\n");
				SDL_Log("\tPC=0x%04x *pStack=%X \n", m_PC, *m_pStack);
				
				break;
			}

			DEBUG_LOG("Missing or invalid opcode 0x%04X", m_opcode);
			break;

		// Jumps to address NNN
		case 0x1:
			m_PC = NNN;
			
			DEBUG_LOG("Jumped to address 0x%03X", m_PC);
			break;

		// Calls subroutine at address NNN
		case 0x2:
			*m_pStack++ = m_PC;
			m_PC = NNN;

			DEBUG_LOG("Called subroutine at address 0x%03X", m_PC);
			break;
		
		// Skip the next instruction if Vx == NN
		case 0x3:
			if (m_V[X] == NN)
				m_PC += 2;

			DEBUG_LOG("Skipping if V[%01X] == %02x", X, NN);
			break;

		// Skip the next instruction if Vx != NN
		case 0x4:
			if (m_V[X] != NN)
				m_PC += 2;

			DEBUG_LOG("Skipping if V[%01X] != %02x", X, NN);
			break;
		
		// Skip the next instruction if Vx == Vy
		case 0x5:
			// Apparently the instruction must be 5XY0, so if N == 0, it might 
			// be a corrupted rom
			if (N != 0)
			{
				SDL_Log("Opcode 0x%04X is wrong. V[%01X] == V[%01X] will not be evaluated!\n",
						m_opcode, X, Y);
						break;	
			}
			if (m_V[X] == m_V[Y])
				m_PC += 2;
			
			DEBUG_LOG("Skipping if V[%01X] == %02x", X, NN);
			break;
		
		// Set Vx to NN
		case 0x6:
			m_V[X] = NN;
			DEBUG_LOG("V[%01X] set to %02X", X, NN);
			break;
		
		// Adds NN to Vx
		case 0x7:
			m_V[X] += NN;
			DEBUG_LOG("Added %02X to V[%01X]", NN, X);
			break;

		// A whole lot of operations
		case 0x8:
			switch (N)
			{
			// Set Vx = Vy
			case 0x0:
				m_V[X] = m_V[Y];

				DEBUG_LOG("V[%01X] = V[%01X] == 0x%01X", X, Y, m_V[X]);
				break;
			
			// Set Vx |= Vy
			case 0x1:
				m_V[X] |= m_V[Y];
				m_V[0xF] = 0;

				DEBUG_LOG("V[%01X] |= V[%01X] == 0x%01X", X, Y, m_V[X]);
				break;

			// Set Vx &= Vy
			case 0x2:
				m_V[X] &= m_V[Y];
				m_V[0xF] = 0;

				DEBUG_LOG("V[%01X] &= V[%01X] == 0x%01X", X, Y, m_V[X]);
				break;
			
			// Set Vx ^= Vy
			case 0x3:
				m_V[X] ^= m_V[Y];
				m_V[0xF] = 0;

				DEBUG_LOG("V[%01X] ^= V[%01X] == 0x%01X", X, Y, m_V[X]);
				break;
			
			// Set Vx += Vy and VF to true if carry
			case 0x4:
				carry = (static_cast<uint16_t>(m_V[X]) + m_V[Y]) > 255;
				m_V[X] += m_V[Y];
				m_V[0xF] = carry;

				DEBUG_LOG("V[%01X] += V[%01X] == 0x%01X", X, Y, m_V[X]);
				break;

			// Set Vx -= Vy and VF if no borrow
			case 0x5:
				carry = m_V[X] >= m_V[Y];
				m_V[X] -= m_V[Y];
				m_V[0xF] = carry;

				DEBUG_LOG("V[%01X] -= V[%01X] == 0x%01X", X, Y, m_V[X]);
				break;

			// Set Vx >>= 1 and VF to the lost bit
			case 0x6:
				carry = m_V[Y] & 1;
				m_V[X] = m_V[Y] >> 1;
				m_V[0xF] = carry;

				DEBUG_LOG("V[%01X] >>= 1 == 0x%01X", X, m_V[X]);
				break;

			// Set Vx = Vy - Vx and VF if no borrow
			case 0x7:
				carry = m_V[X] <= m_V[Y];
				m_V[X] = m_V[Y] - m_V[X];
				m_V[0xF] = carry;

				DEBUG_LOG("V[%01X] = V[%01X] - V[%01X] == 0x%01X", X, Y, X, m_V[X]);
				break;

			// Set Vx <<= 1 and store the lost bit in VF
			case 0xE:
				carry = m_V[Y] >> 7;
				m_V[X] = m_V[Y] << 1;
				m_V[0xF] = carry;

				DEBUG_LOG("V[%01X] <<= 1 == 0x%01X", X, m_V[X]);
				break;
			
			default:
				DEBUG_LOG("Missing or invalid opcode 0x%04X", m_opcode);
				break;
			}
			break;

		// Skip the next instruction if Vx != Vy
		case 0x9:
			if (N != 0)
			{
				SDL_Log("Opcode 0x%04X is wrong. V[%01X] != V[%01X] will not be evaluated!\n",
						m_opcode, X, Y);
						break;
			}
			if (m_V[X] != m_V[Y])
				m_PC += 2;
			
			DEBUG_LOG("Skipping if V[%01X] != %02x", X, NN);
			break;

		// Set I to address NNN
		case 0xA:
			m_I = NNN;

			DEBUG_LOG("I set to the address 0x%04X", NNN);
			break;

		// Jumping to address NNN + V0
		case 0xB:
			m_PC = m_V[0] + NNN;

			DEBUG_LOG("Jumping to address 0x%04X + V[0]", NNN);
			break;
		
		// Random number generator
		case 0xC:
			m_V[X] = Random::get(0, 255) & NN;

			DEBUG_LOG("Generating a random number for V[%01X] and then do bitwise AND with 0x%02X", X, NN);
			break;
		
		// Draw at position X and Y with the N height
		case 0xD:
		{
			// this shit was annoying af
			uint8_t xCoord = m_V[X] % m_scrWidth;
			uint8_t yCoord = m_V[Y] % m_scrHeight;
			uint8_t origin = xCoord;
			m_V[0xF] = 0;

			constexpr uint8_t spriteWidth = 8;

			// For each row(basically drawing on Y axis)
			for (int i = 0; i < N; i++)
			{
				const uint8_t sprite = (*m_ram)[m_I+i];
				xCoord = origin;

				for (int j = spriteWidth - 1; j >= 0; j--)
				{
					bool& pixel = m_display[xCoord+(m_scrWidth*yCoord)];
					const bool spriteBit = (1 << j) & sprite;

					if (spriteBit && pixel)
					{
						m_V[0xF] = true;
					}

					pixel ^= spriteBit;

					// If it hit the right edge of the screen
					if (++xCoord >= m_scrWidth) break;
				}
				// If it hit the bottom edge of the screen
				if (++yCoord >= m_scrHeight) break;
			}
			
			m_draw = true;

			DEBUG_LOG("Drawing at position X=%d and Y=%d with the height %d", X, Y, N);
			break;
		}
		
		// Key inputs
		case 0xE:
			// If the key is pressed
			if (NN == 0x9E)
			{
				if (keypad[m_V[X]])
					m_PC += 2;
				
				DEBUG_LOG("Skipping if the key pressed is %01X", m_V[X]);
				break;
			}
			
			// If the key is not pressed
			if (NN == 0xA1)
			{
				if (!keypad[m_V[X]])
					m_PC += 2;
				
				DEBUG_LOG("Skipping if the key pressed is not %01X", m_V[X]);
				break;
			}
			// If reaching this spot, the opcode is invalid
			SDL_Log("Opcode 0x%04X might be invalid. No key inputs will be detected\n", m_opcode);
			
			break;

		case 0xF:
			switch (NN)
			{
			// Set Vx = delay timer
			case 0x07:
				m_V[X] = m_delayTimer;
				DEBUG_LOG("Set V[%01X] to the clock timer %01X", X, m_V[X]);
				break;
			
			// Getting the keypresses
			case 0x0A:
			{
				DEBUG_LOG("Await for keypresses and then store it in V[%01X]", X);
				
				static bool keyPressed = false;
				// Since the keys go up to 0x0F, 0xFF can be used like null
				static uint8_t key = 0xFF;

				for (uint8_t i = 0; key == 0xFF && i < keypad.size(); i++) 
                    if (keypad[i]) 
					{
						keyPressed = true;
                        key = i;
                        break;
                    }
				
				// If no key has been pressed yet or it is held, keep getting 
				// the current opcode
				if (!keyPressed || keypad[key])
				{
					m_PC -= 2;
				}
				else
				{
					m_V[X] = key;

					keyPressed = false;
					key = 0xFF;
				}
				
				break;
			}
			// Set delay timer = Vx
			case 0x15:
				m_delayTimer = m_V[X];
				DEBUG_LOG("Set the delay timer of %01X to V[%01X]", m_V[X], X);
				break;

			// Set sound timer = Vx
			case 0x18:
				m_soundTimer = m_V[X];
				DEBUG_LOG("Set the sound timer of %01X to V[%01X]", m_V[X], X);
				break;

			// Adds Vx to I
			case 0x1E:
				m_I += m_V[X];
				DEBUG_LOG("I += V[%01X] == 0x%01X", X, m_V[X]);
				break;

			// Sets I to the sprite location for the char in Vx
			case 0x29:
				m_I = m_V[X] * 5;
				DEBUG_LOG("I = V[%01X] * 5 == 0x%01X", X, m_V[X]);
				break;

			// Binary to decimal conversion
			case 0x33:
			{
				uint8_t BCD = m_V[X];
				(*m_ram)[m_I+2] = BCD % 10;
				BCD /= 10;
				(*m_ram)[m_I+1] = BCD % 10;
				BCD /= 10;
				(*m_ram)[m_I] = BCD;

				DEBUG_LOG("something something BCD");
				break;
			}

			// Dumping the registers from F0 - Vx inclusive
			case 0x55:
			{
				DEBUG_LOG("Dumped the registers up to 0x%02X (inclusive) into the ram. The values are:", X);

				for (uint8_t i = 0; i <= X; i++)
				{
					(*m_ram)[m_I++] = m_V[i];
					DEBUG_LOG("\tV[%01X] = %01X", i, X);
				}

				break;
			}

			// Copying from ram to registers
			case 0x65:
			{				
				DEBUG_LOG("Filled the registers up to 0x%02X (inclusive) with values from ram. The values are:", X);

				for (uint8_t i = 0; i <= X; i++)
				{
					m_V[i] = (*m_ram)[m_I++];
					DEBUG_LOG("\tV[%01X] = %01X", i, X);
				}

				break;
			}
			}
			break;

		default:
			DEBUG_LOG("Missing or invalid opcode 0x%04X", m_opcode);
			break;
		}
	}
};

// Drawing informational UI function
void drawInfo(SDL_Surface* surf, Chip8& chip8)
{
	// Beeping
	if (chip8.isBeeping())
	{
		// TODO: Maybe change this to smth actually nicer like an icon
		// Currently it just makes a square in the top right corner of the
		// chip 8 screen

		SDL_Rect rect{.x=surf->w-32-4, .y=4, .w=32, .h=32};

		SDL_FillRect(surf, &rect, Config::beepIconColor);
	}
}

// Shotting screenshot function
void shootScreenshot(sdl_t& sdl)
{
	if (!std::filesystem::exists(Config::ssDir))
		if (!std::filesystem::create_directory(Config::ssDir))
		{
			SDL_Log("Failed to create directory \"%s\"!", Config::ssDir);
			return;
		}

    // Buffer for the final file path
    char ssPath[256];
    char buffer[64];
    time_t tm;
    struct tm* tmInfo;

    time(&tm);
    tmInfo = localtime(&tm);
    strftime(buffer, sizeof(buffer), "%d_%m_%Y_%H-%M", tmInfo);

    snprintf(ssPath, sizeof(ssPath), "%s/%s_%s.bmp", Config::ssDir, Config::ssPrefix, buffer);

	SDL_Surface* surf = SDL_GetWindowSurface(sdl.window);
	SDL_SaveBMP(surf, ssPath);

    SDL_Log("Saved screenshot to \"%s\"\n", ssPath);
}

// Main loop function
void loop(sdl_t& sdl, Chip8& chip8)
{
	chip8.loadProgram(Config::romPath);
	SDL_Surface* chip8Surf = SDL_CreateRGBSurface(0, 
							chip8.getWidth()*Config::scaleFac,
							chip8.getHeight()*Config::scaleFac, 32, 0, 0, 0, 0);

	SDL_FillRect(chip8Surf, 0, Config::bgColor);

	bool running = true;
	while (running)
	{
		const double startFrame = SDL_GetPerformanceCounter();
		bool screenshot = false;

		// Update the window surface
		SDL_Surface* winSurf = SDL_GetWindowSurface(sdl.window);
		
		SDL_Event ev;
		while (SDL_PollEvent(&ev))
		{
			switch (ev.type)
			{
			case SDL_QUIT:
				running = false;
				break;

			case SDL_KEYDOWN:
                switch (ev.key.keysym.sym) 
				{
				case SDLK_ESCAPE:
					running = false;
					break;
				
				case SDLK_PERIOD:
					if (Global::clockSpeed + 100 < Config::normalClockSpeed * Config::maxClockSpeedMp)
						Global::clockSpeed += 100;
					break;

				case SDLK_COMMA:
					if (Global::clockSpeed - 100 > Config::normalClockSpeed * Config::minClockSpeedMp)
						Global::clockSpeed -= 100;
					break;
				
				case SDLK_0:
					Global::clockSpeed = Config::normalClockSpeed;
					break;
				
				case SDLK_SPACE:
				if (Global::clockSpeed == 0)
				{
					Global::clockSpeed = Config::normalClockSpeed;
					printf("+---Unpaused---+\n");
					break;
				}
					Global::clockSpeed = 0;
					printf("+---Paused---+\n");
					break;
				
				case SDLK_BACKQUOTE:
					screenshot = true;
					break;
				
				// Map qwerty keys to CHIP8 keypad
				case SDLK_1: chip8.keypad[0x1] = true; break;
				case SDLK_2: chip8.keypad[0x2] = true; break;
				case SDLK_3: chip8.keypad[0x3] = true; break;
				case SDLK_4: chip8.keypad[0xC] = true; break;

				case SDLK_q: chip8.keypad[0x4] = true; break;
				case SDLK_w: chip8.keypad[0x5] = true; break;
				case SDLK_e: chip8.keypad[0x6] = true; break;
				case SDLK_r: chip8.keypad[0xD] = true; break;

				case SDLK_a: chip8.keypad[0x7] = true; break;
				case SDLK_s: chip8.keypad[0x8] = true; break;
				case SDLK_d: chip8.keypad[0x9] = true; break;
				case SDLK_f: chip8.keypad[0xE] = true; break;

				case SDLK_z: chip8.keypad[0xA] = true; break;
				case SDLK_x: chip8.keypad[0x0] = true; break;
				case SDLK_c: chip8.keypad[0xB] = true; break;
				case SDLK_v: chip8.keypad[0xF] = true; break;
				
				default: break;
                }

                break; 

            case SDL_KEYUP:
                switch (ev.key.keysym.sym) 
				{
				// Map qwerty keys to CHIP8 keypad
				case SDLK_1: chip8.keypad[0x1] = false; break;
				case SDLK_2: chip8.keypad[0x2] = false; break;
				case SDLK_3: chip8.keypad[0x3] = false; break;
				case SDLK_4: chip8.keypad[0xC] = false; break;

				case SDLK_q: chip8.keypad[0x4] = false; break;
				case SDLK_w: chip8.keypad[0x5] = false; break;
				case SDLK_e: chip8.keypad[0x6] = false; break;
				case SDLK_r: chip8.keypad[0xD] = false; break;

				case SDLK_a: chip8.keypad[0x7] = false; break;
				case SDLK_s: chip8.keypad[0x8] = false; break;
				case SDLK_d: chip8.keypad[0x9] = false; break;
				case SDLK_f: chip8.keypad[0xE] = false; break;

				case SDLK_z: chip8.keypad[0xA] = false; break;
				case SDLK_x: chip8.keypad[0x0] = false; break;
				case SDLK_c: chip8.keypad[0xB] = false; break;
				case SDLK_v: chip8.keypad[0xF] = false; break;

				default: break;
            	}

                break;
			}
		}

		bool screenRefreshed = false;

		// Emulate instructions at a speed of 60hz
		for (int i = 0; i < Global::clockSpeed / 60; i++)
		{
			// Emulate a cycle
			chip8.emulateCycle();

			// Break if the screen needs to be redrawn
			if (chip8.refreshScreen())
			{
				// Looks like a bit of a workaround but it looks nicer imo
				screenRefreshed = true;
				break;
			}
		}

		const double endFrame = SDL_GetPerformanceCounter();

		// Get the time elapsed since the previous frame and delay it by 60hz/s
		const double timeElapsed = (endFrame - startFrame) * 1000.0 / SDL_GetPerformanceFrequency();
		SDL_Delay(16.67f > timeElapsed ? 16.67f - timeElapsed : 0);

		// Redraw the chip 8 screen
		if (screenRefreshed)
			chip8.drawDisplay(chip8Surf);
		
		// This uses a separate surface for the chip8 display so it can be put 
		// anywhere on the window. Might reuse in case I want to add UI
		SDL_Rect chip8DisplayRect;
		chip8DisplayRect.x = (winSurf->w - chip8.getWidth() * Config::scaleFac)/2;
		chip8DisplayRect.y = (winSurf->h - chip8.getHeight() * Config::scaleFac)/2;
		chip8DisplayRect.w = chip8.getWidth() * Config::scaleFac;
		chip8DisplayRect.h = chip8.getHeight() * Config::scaleFac;

		SDL_BlitScaled(chip8Surf, NULL, winSurf, &chip8DisplayRect);

		chip8.updateTimers();

		drawInfo(winSurf, chip8);

		SDL_UpdateWindowSurface(sdl.window);

		if (screenshot)
			shootScreenshot(sdl);
		
		// Cleanup (prob shouldve been put in the clean() function)
		if (!running)
			SDL_FreeSurface(chip8Surf);
	}
}

// Startup arguments handler function
bool handleArgs(const int argc, char* argv[])
{
	// TODO: add more args

	if (argc < 2)
	{
		SDL_Log("Usage: %s <rom name>\n", argv[0]);
		return false;
	}

	Config::romPath = argv[1];
	SDL_Log("Running %s\n", argv[1]);

	return true;
}

// Initializitation function
bool init(sdl_t& sdl, Chip8& c8)
{
	if (SDL_InitSubSystem(SDL_INIT_AUDIO | SDL_INIT_VIDEO | SDL_INIT_TIMER))
	{
		SDL_Log("Failed to initialize SDL subsystems: %s\n", SDL_GetError());
		return false;
	}
	sdl.window = SDL_CreateWindow(Config::title, SDL_WINDOWPOS_CENTERED, 
					SDL_WINDOWPOS_CENTERED, c8.getWidth() * Config::scaleFac,
					c8.getHeight() * Config::scaleFac, 0);

	if (!sdl.window)
	{
		SDL_Log("Could not create window: %s\n", SDL_GetError());
		return false;
	}
					
	return true;
}

// Cleanup function
void clean(sdl_t& sdl)
{
	SDL_DestroyWindow(sdl.window);
	SDL_Quit();
}

// Datz mein
int main(int argc, char* argv[])
{
	if (!handleArgs(argc, argv)) return 1;

	sdl_t sdl{};
	Chip8 chip8{};

	if (!init(sdl, chip8)) return 1;

	loop(sdl, chip8);

	clean(sdl);
	return 0;
}
