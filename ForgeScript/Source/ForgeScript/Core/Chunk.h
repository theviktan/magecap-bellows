#pragma once

#include "ForgeScript/Core/Value.h"

#include <cstdint>
#include <vector>

namespace ForgeScript {

	// Every instruction the ForgeScript virtual machine understands.
	// Operand sizes are listed next to each opcode: u8 = 1 byte, u16 = 2 bytes (big endian).
	enum class OpCode : uint8_t
	{
		Constant,        // u16 constant index        -> push constant
		Nothing,         //                           -> push nothing
		True,            //                           -> push true
		False,           //                           -> push false
		Pop,             // pop one value
		Dup,             // duplicate top value
		Dup2,            // duplicate the top two values (a b -> a b a b)

		GetLocal,        // u8 slot
		SetLocal,        // u8 slot (value stays on the stack)
		GetUpvalue,      // u8 index
		SetUpvalue,      // u8 index (value stays on the stack)
		CloseUpvalue,    // close the upvalue at the top of the stack, then pop it
		GetGlobal,       // u16 name constant
		SetGlobal,       // u16 name constant (creates the variable if needed, value stays)
		DefineConstant,  // u16 name constant (pops value)

		GetProperty,     // u16 name constant         obj -> value
		SetProperty,     // u16 name constant         obj value -> value
		GetIndex,        //                           obj index -> value
		SetIndex,        //                           obj index value -> value
		WriteBackProperty, // u16 name constant       parent value -> parent (stores value if it is a vector)
		WriteBackIndex,  //                           parent index value -> parent (stores value if it is a vector)

		Equal,
		NotEqual,
		Greater,
		GreaterEqual,
		Less,
		LessEqual,
		Add,
		Subtract,
		Multiply,
		Divide,
		Modulo,
		Power,
		Negate,
		Not,
		Contains,        // container item -> bool
		In,              // item container -> bool
		IsType,          // u8 TypeCheck              value -> bool
		IsKind,          //                           value kind -> bool
		IsEmpty,         //                           value -> bool

		AddTo,           // current amount -> result   (used by "add ... to ...")
		TakeFrom,        // current amount -> result   (used by "take ... from ...")

		Jump,            // u16 forward offset
		JumpIfFalse,     // u16 forward offset (does not pop)
		JumpIfTrue,      // u16 forward offset (does not pop)
		Loop,            // u16 backward offset

		ForPrepare,      // u8 slot of counter (counter, end, step occupy slot..slot+2)
		ForCheck,        // u8 slot, u16 exit offset
		ForStep,         // u8 slot
		IterNext,        // u8 slot of iterable (iterable, position at slot, slot+1), u16 exit offset
		RepeatCheck,     // u8 slot of count (count, done at slot, slot+1), u16 exit offset

		Call,            // u8 argument count
		Invoke,          // u16 name, u8 argument count
		InvokeParent,    // u16 name, u8 argument count
		GetParent,       // u16 name                  self -> bound method
		Start,           // u8 argument count (spawns a new task instead of calling)
		Closure,         // u16 function constant, then per upvalue: u8 isLocal, u8 index
		Return,

		Kind,            // u16 name constant         -> push new kind
		Inherit,         //                           kind parent -> kind
		Method,          // u16 name constant         kind closure -> kind
		FieldInit,       //                           kind closure -> kind
		New,             // u8 argument count         kind args... -> instance

		NewList,         //                           -> empty list
		ListAppend,      //                           list value -> list
		NewMap,          //                           -> empty map
		MapSet,          //                           map key value -> map

		Interpolate,     // u8 part count             parts... -> text

		TryBegin,        // u16 offset to the catch block
		TryEnd,          //
		Fail,            //                           value -> (raises an error)

		Wait,            // u8 WaitMode               amount -> (pauses the task)
		OnEvent,         // u16 event name            closure ->
		Trigger,         // u16 event name, u8 argument count
		Use,             // u16 path constant, u8 bindAll (0 = push module, 1 = copy globals)
		Between,         //                           value low high -> bool (both ends included, either order)
		TryFinally,      // u16 offset to the finally code; on an error pushes the error and where it happened
		Rethrow,         //                           error where -> (raises the error again from where it happened)
		NamedArgs,       // u16 list of names         (the next call's last values go to the parameters with these names)
		SkipIfGiven,     // u8 default index, u16 offset: skips a default's code when the call gave that value
		Unpack,          // u8 count                  list or vector -> its items, in order (set x, y to ...)
	};

	enum class TypeCheck : uint8_t
	{
		Number = 0,
		Text,
		Boolean,
		List,
		Map,
		Function,
		Vector,
		Nothing,
		Kind,
		Object
	};

	enum class WaitMode : uint8_t
	{
		Seconds = 0,
		Frames
	};

	// A Chunk is a compiled block of bytecode plus the constants it refers to.
	class Chunk
	{
	public:
		void Write(uint8_t byte, int line)
		{
			m_Code.push_back(byte);
			m_Lines.push_back(line);
		}

		void Write(OpCode op, int line) { Write(static_cast<uint8_t>(op), line); }

		size_t AddConstant(Value value)
		{
			for (size_t i = 0; i < m_Constants.size(); i++)
			{
				// Reuse identical constants (numbers, interned text) to keep chunks small.
				if (m_Constants[i] == value && m_Constants[i].GetType() == value.GetType())
					return i;
			}
			m_Constants.push_back(value);
			return m_Constants.size() - 1;
		}

		std::vector<uint8_t>& GetCode() { return m_Code; }
		const std::vector<uint8_t>& GetCode() const { return m_Code; }
		const std::vector<Value>& GetConstants() const { return m_Constants; }
		int GetLine(size_t offset) const { return offset < m_Lines.size() ? m_Lines[offset] : 0; }
		size_t Size() const { return m_Code.size(); }

		// Code cut out of a chunk with its line numbers, to be put back further on (the compiler moves
		// a value after the test in 'value if test otherwise other'). Jumps are relative, so the code
		// works wherever it lands.
		struct Piece
		{
			std::vector<uint8_t> Code;
			std::vector<int> Lines;
		};

		Piece Cut(size_t offset)
		{
			Piece piece;
			piece.Code.assign(m_Code.begin() + static_cast<std::ptrdiff_t>(offset), m_Code.end());
			piece.Lines.assign(m_Lines.begin() + static_cast<std::ptrdiff_t>(offset), m_Lines.end());
			m_Code.resize(offset);
			m_Lines.resize(offset);
			return piece;
		}

		void Paste(const Piece& piece)
		{
			m_Code.insert(m_Code.end(), piece.Code.begin(), piece.Code.end());
			m_Lines.insert(m_Lines.end(), piece.Lines.begin(), piece.Lines.end());
		}

	private:
		std::vector<uint8_t> m_Code;
		std::vector<int> m_Lines;
		std::vector<Value> m_Constants;
	};

}
