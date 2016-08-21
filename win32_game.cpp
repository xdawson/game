#define global static
#define internal static
#define local_persist static

#define PI32 3.14159265359f

#include <stdint.h>

typedef int8_t int8;
typedef int16_t int16;
typedef int32_t int32;
typedef int64_t int64;

typedef int32 bool32;

typedef uint8_t uint8;
typedef uint16_t uint16;
typedef uint32_t uint32;
typedef uint64_t uint64;

typedef float real32;
typedef double real64;

#include <math.h>

#include "Game.h"
#include "Game.cpp"

#include <windows.h>
#include <dsound.h>

struct win32_window_dimension
{
	int Width;
	int Height;
};

struct win32_offscreen_buffer
{
	BITMAPINFO Info;
	void *Memory;
	int Width;
	int Height;
	int Pitch;
};

global bool GlobalRunning;
global win32_offscreen_buffer GlobalBuffer;
global bool GlobalUp;
global bool GlobalLeft;
global LPDIRECTSOUNDBUFFER GlobalSecondaryBuffer;
global bool GlobalSoundIsPlaying;


#define DIRECT_SOUND_CREATE(name) HRESULT WINAPI name(LPCGUID pcGuidDevice, LPDIRECTSOUND *ppDS, LPUNKNOWN pUnkOuter)
typedef DIRECT_SOUND_CREATE(direct_sound_create);

internal win32_window_dimension
Win32GetWindowDimension(HWND Window)
{
	win32_window_dimension Result;

	RECT ClientRect;
	GetClientRect(Window, &ClientRect);

	Result.Width = ClientRect.right - ClientRect.left;
	Result.Height = ClientRect.bottom - ClientRect.top;

	return Result;
}
internal void 
Win32ResizeDIBSection(win32_offscreen_buffer *Buffer, int Width, int Height)
{
	if (Buffer->Memory)
	{
		VirtualFree(Buffer->Memory, 0, MEM_RELEASE);
	}
	
	Buffer->Info.bmiHeader.biSize = sizeof(Buffer->Info.bmiHeader);
	Buffer->Info.bmiHeader.biWidth = Width;
	Buffer->Info.bmiHeader.biHeight = -Height;
	Buffer->Info.bmiHeader.biPlanes = 1;
	Buffer->Info.bmiHeader.biBitCount = 32;
	Buffer->Info.bmiHeader.biCompression = BI_RGB;

	Buffer->Width = Width;
	Buffer->Height = Height;
	int BytesPerPixel = 4;
	Buffer->Pitch = Width * BytesPerPixel;
	int BufferSize = (Width*Height) * BytesPerPixel;

	Buffer->Memory = VirtualAlloc(0, BufferSize, MEM_COMMIT, PAGE_READWRITE);
}

internal void
Win32InitDSound(HWND Window, uint32 SamplesPerSecond, uint32 BufferSize)
{
	//Load the Library
	HMODULE DSoundLibrary = LoadLibraryA("dsound.dll");
	if (DSoundLibrary)
	{
		//Create DirectSound object
		direct_sound_create *DirectSoundCreate = (direct_sound_create *)GetProcAddress(DSoundLibrary, "DirectSoundCreate");

		LPDIRECTSOUND DirectSound;
		if (DirectSoundCreate && SUCCEEDED(DirectSoundCreate(0, &DirectSound, 0)))
		{
			WAVEFORMATEX WaveFormat = {};

			WaveFormat.wFormatTag = WAVE_FORMAT_PCM;
			WaveFormat.nChannels = 2;
			WaveFormat.nSamplesPerSec = SamplesPerSecond;
			WaveFormat.wBitsPerSample = 16;
			WaveFormat.nBlockAlign = (WaveFormat.nChannels * WaveFormat.wBitsPerSample) / 8;
			WaveFormat.nAvgBytesPerSec = WaveFormat.nSamplesPerSec * WaveFormat.nBlockAlign;

			if (SUCCEEDED(DirectSound->SetCooperativeLevel(Window, DSSCL_PRIORITY)))
			{
				DSBUFFERDESC BufferDescription = {};
				BufferDescription.dwSize = sizeof(BufferDescription);
				BufferDescription.dwFlags = DSBCAPS_PRIMARYBUFFER;

				//Create Primary buffer
				LPDIRECTSOUNDBUFFER PrimaryBuffer;
				if (SUCCEEDED(DirectSound->CreateSoundBuffer(&BufferDescription, &PrimaryBuffer, 0)))
				{
					if (SUCCEEDED(PrimaryBuffer->SetFormat(&WaveFormat)))
					{
						//Primary Buffer's format set!
					}
				}
			}

			DSBUFFERDESC BufferDescription = {};
			BufferDescription.dwSize = sizeof(BufferDescription);
			BufferDescription.dwBufferBytes = BufferSize;
			BufferDescription.lpwfxFormat = &WaveFormat;

			//Create Secondary buffer
			if (SUCCEEDED(DirectSound->CreateSoundBuffer(&BufferDescription, &GlobalSecondaryBuffer, 0)))
			{
				//Secondary Buffer created!
			}
		}
	}
}

internal void
Win32DisplayBufferInWindow(win32_offscreen_buffer Buffer, win32_window_dimension Dimension, HDC DeviceContext)
{
	StretchDIBits(
		DeviceContext,
		0, 0, Dimension.Width, Dimension.Height,
		0, 0, Buffer.Width, Buffer.Height,
		Buffer.Memory,
		&Buffer.Info,
		DIB_RGB_COLORS,
		SRCCOPY
		);
}

struct win32_sound_output
{
	int SamplesPerSecond;
	uint32 RunningSampleIndex;
	int BytesPerSample;
	int LatencySampleCount;
	int SecondaryBufferSize;
};

internal void
Win32ClearSoundBuffer(win32_sound_output *SoundOutput)
{
	VOID *Region1;
	DWORD Region1Size;
	VOID *Region2;
	DWORD Region2Size;
	if (SUCCEEDED(GlobalSecondaryBuffer->Lock(
		0, SoundOutput->SecondaryBufferSize,
		&Region1, &Region1Size,
		&Region2, &Region2Size,
		0)))
	{
		DWORD Region1SampleCount = Region1Size;
		uint8 *DestSample = (uint8 *)Region1;
		for (uint32 SampleIndex = 0;
			SampleIndex < Region1SampleCount;
			SampleIndex++)
		{
			*DestSample++ = 0;
		}

		DWORD Region2SampleCount = Region2Size;
		DestSample = (uint8 *)Region2;
		for (uint32 SampleIndex = 0;
			SampleIndex < Region2SampleCount;
			SampleIndex++)
		{
			*DestSample++ = 0;
		}
	}

	GlobalSecondaryBuffer->Unlock(Region1, Region1Size, Region2, Region2Size);
}

internal void
Win32FillSoundBuffer(win32_sound_output *SoundOutput, game_sound_output_buffer *SourceBuffer, DWORD ByteToLock, DWORD BytesToWrite)
{
	VOID *Region1;
	DWORD Region1Size;
	VOID *Region2;
	DWORD Region2Size;
	if (SUCCEEDED(GlobalSecondaryBuffer->Lock(
		ByteToLock, BytesToWrite,
		&Region1, &Region1Size,
		&Region2, &Region2Size,
		0)))
	{
		DWORD Region1SampleCount = Region1Size / SoundOutput->BytesPerSample;
		int16 *DestSample = (int16 *)Region1;
		int16 *SourceSample = (int16 *)SourceBuffer->Samples;
		for (uint32 SampleIndex = 0;
			SampleIndex < Region1SampleCount;
			SampleIndex++)
		{
			*DestSample++ = *SourceSample++;
			*DestSample++ = *SourceSample++;
			SoundOutput->RunningSampleIndex++;
		}

		DWORD Region2SampleCount = Region2Size / SoundOutput->BytesPerSample;
		DestSample = (int16 *)Region2;
		for (uint32 SampleIndex = 0;
			SampleIndex < Region2SampleCount;
			SampleIndex++)
		{
			*DestSample++ = *SourceSample++;
			*DestSample++ = *SourceSample++;
			SoundOutput->RunningSampleIndex++;
		}

		GlobalSecondaryBuffer->Unlock(Region1, Region1Size, Region2, Region2Size);
	}
}


internal LRESULT CALLBACK 
WindowProc(
	HWND   Window,
	UINT   Message,
	WPARAM WParam,
	LPARAM LParam
	)
{
	LRESULT Result = 0;
	
	switch (Message) 
	{
		case WM_DESTROY:
		{
			//could be an error? maybe try recreating the window?
			GlobalRunning = false;
		} break;

		case WM_CLOSE:
		{
			//a user has tried to close the window, could post "are you sure" message to user
			GlobalRunning = false;
		} break;

		case WM_ACTIVATEAPP:
		{
			OutputDebugStringA("WM_ACTIVATEAPP\n");
		} break;

		case WM_PAINT:
		{
			PAINTSTRUCT Paint;
			HDC DeviceContext = BeginPaint(Window, &Paint);
			
			int X = Paint.rcPaint.left;
			int Y = Paint.rcPaint.top;
			int Width = Paint.rcPaint.right - Paint.rcPaint.left;
			int Height = Paint.rcPaint.bottom - Paint.rcPaint.top;

			win32_window_dimension Dimension = Win32GetWindowDimension(Window);

			Win32DisplayBufferInWindow(GlobalBuffer, Dimension, DeviceContext);
			
			EndPaint(Window, &Paint);
		} break;

		case WM_KEYDOWN:
		{
			switch (WParam)
			{
				case 'W':
				{
					GlobalUp = true;
				} break;

				case 'A':
				{
					GlobalLeft = true;
				} break;

				case 'S':
				{
					GlobalUp = false;
				} break;

				case 'D':
				{
					GlobalLeft = false;
				} break;
			}
		}

		default:
		{
			Result = DefWindowProcA(Window, Message, WParam, LParam);
		} break;
	}

	return(Result);
}


int CALLBACK 
WinMain(HINSTANCE Instance,
		HINSTANCE PrevInstance,
		LPSTR     lpCmdLine,
		int       nCmdShow) 
{
	WNDCLASSA WindowClass = {};
	
	Win32ResizeDIBSection(&GlobalBuffer, 1280, 720);
	
	WindowClass.style = CS_OWNDC|CS_HREDRAW|CS_VREDRAW;
	WindowClass.lpfnWndProc = WindowProc;
	WindowClass.hInstance = Instance;
	//WindowClass.hIcon = ;
	WindowClass.lpszClassName = "My Window Class";

	if (RegisterClassA(&WindowClass)) {

		HWND Window = CreateWindowExA(
			0,
			WindowClass.lpszClassName,
			"Game",
			WS_OVERLAPPEDWINDOW | WS_VISIBLE,
			CW_USEDEFAULT,
			CW_USEDEFAULT,
			CW_USEDEFAULT,
			CW_USEDEFAULT,
			0,
			0,
			Instance,
			0);

		if (Window) 
		{
			//Since I specified CS_OWNDC I can just get one device context and use it forever
			//because I'm not sharing it with anyone
			//no release required
			HDC DeviceContext = GetDC(Window);
			
			//Sound test
			win32_sound_output SoundOutput = {};
			
			SoundOutput.SamplesPerSecond = 48000;
			SoundOutput.RunningSampleIndex = 0;
			SoundOutput.BytesPerSample = sizeof(int16) * 2;
			SoundOutput.LatencySampleCount = SoundOutput.SamplesPerSecond / 15;
			SoundOutput.SecondaryBufferSize = SoundOutput.SamplesPerSecond * SoundOutput.BytesPerSample;

			Win32InitDSound(Window, SoundOutput.SamplesPerSecond, SoundOutput.SecondaryBufferSize);
			Win32ClearSoundBuffer(&SoundOutput);

			GlobalSecondaryBuffer->Play(0, 0, DSBPLAY_LOOPING);


			GlobalRunning = true;

			int16 *Samples = (int16 *)VirtualAlloc(0, SoundOutput.SecondaryBufferSize, 
										  MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);

			//Graphics Test
			int XOffset = 0;
			int YOffset = 0;

			while(GlobalRunning) 
			{
				MSG Message;
				while(PeekMessageA(&Message, Window, 0, 0, PM_REMOVE))
				{
					if (Message.message == WM_QUIT)
					{
						GlobalRunning = false;
					}

					TranslateMessage(&Message);
					DispatchMessageA(&Message);
				}


				DWORD ByteToLock = 0;
				DWORD TargetCursor = 0;
				DWORD BytesToWrite = 0;  
				DWORD PlayCursor = 0;
				DWORD WriteCursor = 0;
				bool32 SoundIsValid = false;

				if (SUCCEEDED(GlobalSecondaryBuffer->GetCurrentPosition(&PlayCursor, &WriteCursor)))
				{

					ByteToLock = (SoundOutput.RunningSampleIndex * SoundOutput.BytesPerSample) % 
									SoundOutput.SecondaryBufferSize;

					TargetCursor = (PlayCursor + 
								    (SoundOutput.LatencySampleCount * SoundOutput.BytesPerSample))
								      % SoundOutput.SecondaryBufferSize;
					
					if (ByteToLock > TargetCursor)
					{
						BytesToWrite = (SoundOutput.SecondaryBufferSize - ByteToLock);
						BytesToWrite += TargetCursor;
					}	
					else
					{
						BytesToWrite = TargetCursor - ByteToLock; 
					}

					SoundIsValid = true;
				}

				game_sound_output_buffer SoundBuffer = {};
				SoundBuffer.SamplesPerSecond = SoundOutput.SamplesPerSecond;
				SoundBuffer.SampleCount = BytesToWrite / SoundOutput.BytesPerSample;
				SoundBuffer.Samples = Samples;
				
				//Graphics Code
				game_offscreen_buffer Buffer = {};
				Buffer.Memory = GlobalBuffer.Memory;
				Buffer.Width = GlobalBuffer.Width;
				Buffer.Height = GlobalBuffer.Height;
				Buffer.Pitch = GlobalBuffer.Pitch;
				
				GameUpdateAndRender(&Buffer, &SoundBuffer, XOffset, YOffset);

				if(SoundIsValid)
				{
					Win32FillSoundBuffer(&SoundOutput, &SoundBuffer, ByteToLock, BytesToWrite);
				}

				
				win32_window_dimension Dimension = Win32GetWindowDimension(Window);
				Win32DisplayBufferInWindow(GlobalBuffer, Dimension, DeviceContext);

				//Move the backbuffer around
				if (GlobalLeft)
				{
					XOffset ++;
				}
				else if (!GlobalLeft)
				{
					XOffset --;
				}

				if (GlobalUp)
				{
					YOffset ++;
				}
				else if (!GlobalUp)
				{
					YOffset --;
				}
			}
		}
	}
	return 0;
}