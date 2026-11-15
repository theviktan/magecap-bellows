// A tiny stand-in for a game engine, showing every step of embedding Forge:
//   1. one VM for the whole game
//   2. an "Entity" type that scripts can read, change and call
//   3. engine phrases such as  move (thing) toward (target) by (amount)
//   4. one script module per entity, with "self" set to that entity
//   5. events sent from the game loop, and time moving forward with Update
//   6. destroying entities safely
//
// Build target: ForgeEngineExample. Run it from the build folder.

#include "Forge/Forge.h"

#include <cmath>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

namespace Example {

	// ------------------------------------------------------------------ The "engine"

	struct Entity
	{
		uint64_t ID = 0;
		std::string Name;
		double X = 0, Y = 0, Z = 0;
		double Health = 100;
		bool Alive = true;
		Forge::Module* Script = nullptr;
	};

	// Scripts hold a small reference object instead of a raw Entity pointer, so a script
	// that keeps an entity in a variable gets a clear error after the entity is destroyed.
	struct EntityRef
	{
		class Scene* Owner = nullptr;
		uint64_t ID = 0;
	};

	class Scene
	{
	public:
		Entity* Create(const std::string& name, double x)
		{
			auto entity = std::make_unique<Entity>();
			entity->ID = ++m_NextID;
			entity->Name = name;
			entity->X = x;
			m_Entities.push_back(std::move(entity));
			return m_Entities.back().get();
		}

		Entity* Find(uint64_t id)
		{
			for (auto& entity : m_Entities)
			{
				if (entity->ID == id && entity->Alive)
					return entity.get();
			}
			return nullptr;
		}

		std::vector<std::unique_ptr<Entity>>& All() { return m_Entities; }

	private:
		std::vector<std::unique_ptr<Entity>> m_Entities;
		uint64_t m_NextID = 0;
	};

	// ------------------------------------------------------------------ Script bindings

	class ScriptEngine
	{
	public:
		explicit ScriptEngine(Scene& scene)
			: m_Scene(scene), m_VM(MakeConfig())
		{
			m_VM.SetPrintHandler([](const std::string& text) { std::cout << "[script] " << text << std::endl; });
			m_VM.SetErrorHandler([](const Forge::ScriptError& error) { std::cerr << "[script error] " << error.ToString() << std::endl; });
			RegisterEntityType();
			RegisterPhrases();
		}

		// Step 4: every entity with a script gets its own module.
		bool AttachScript(Entity* entity, const std::string& source, const std::string& fileName)
		{
			entity->Script = m_VM.CreateModule(fileName);
			m_VM.SetGlobal(entity->Script, "self", Wrap(entity));
			return m_VM.RunInModule(entity->Script, source, fileName).Success;
		}

		// Step 5: called once per frame by the game loop.
		void Tick(double deltaSeconds)
		{
			for (auto& entity : m_Scene.All())
			{
				if (entity->Alive && entity->Script)
					m_VM.Emit(entity->Script, "update", { Forge::Value::FromNumber(deltaSeconds) });
			}
			m_VM.Update(deltaSeconds);
		}

		void SendEvent(Entity* entity, const std::string& event, const std::vector<Forge::Value>& values)
		{
			if (entity->Alive && entity->Script)
				m_VM.Emit(entity->Script, event, values);
		}

		Forge::Value Wrap(Entity* entity)
		{
			return m_VM.MakeNative(m_EntityType, new EntityRef{ &m_Scene, entity->ID });
		}

		// Step 6: destroy an entity and stop its script.
		void Destroy(Entity* entity)
		{
			entity->Alive = false;
			if (entity->Script)
			{
				m_VM.UnloadModule(entity->Script);
				entity->Script = nullptr;
			}
		}

		Forge::VM& GetVM() { return m_VM; }

	private:
		static Forge::VMConfig MakeConfig()
		{
			Forge::VMConfig config;
			config.InstructionLimit = 10'000'000; // A script that never waits cannot freeze the game.
			return config;
		}

		static Entity* Resolve(void* data)
		{
			EntityRef* ref = static_cast<EntityRef*>(data);
			return ref->Owner->Find(ref->ID);
		}

		// Step 2: describe Entity to Forge.
		void RegisterEntityType()
		{
			m_EntityType.Name = "Entity";
			m_EntityType.IsAlive = [](void* data) { return Resolve(data) != nullptr; };
			m_EntityType.Finalizer = [](void* data) { delete static_cast<EntityRef*>(data); };
			m_EntityType.ToText = [](void* data)
			{
				Entity* entity = Resolve(data);
				return entity ? "Entity(" + entity->Name + ")" : std::string("Entity(destroyed)");
			};

			m_EntityType.Getters["name"] = [](Forge::VM& vm, void* data) { return vm.MakeText(Resolve(data)->Name); };
			m_EntityType.Getters["health"] = [](Forge::VM&, void* data) { return Forge::Value::FromNumber(Resolve(data)->Health); };
			m_EntityType.Getters["position"] = [](Forge::VM& vm, void* data)
			{
				Entity* e = Resolve(data);
				return vm.MakeVector(e->X, e->Y, e->Z);
			};

			m_EntityType.Setters["health"] = [](Forge::VM&, void* data, const Forge::Value& value)
			{
				if (!value.IsNumber())
					throw Forge::RuntimeError("health must be a number");
				Resolve(data)->Health = value.AsNumber();
			};
			m_EntityType.Setters["position"] = [](Forge::VM&, void* data, const Forge::Value& value)
			{
				if (!Forge::IsObjectType(value, Forge::ObjectType::Vector))
					throw Forge::RuntimeError("position must be a vector");
				Forge::Vector* v = Forge::As<Forge::Vector>(value);
				Entity* e = Resolve(data);
				e->X = v->X;
				e->Y = v->Y;
				e->Z = v->Z;
			};

			m_EntityType.Methods["destroy"] = { 0, [this](Forge::CallContext& context)
			{
				// SelfAs reports a clear script error if the entity was already destroyed.
				EntityRef* ref = context.SelfAs<EntityRef>(m_EntityType);
				Destroy(Resolve(ref));
				return Forge::Value();
			} };
		}

		// Step 3: engine commands written as phrases.
		void RegisterPhrases()
		{
			m_VM.RegisterPhrase("move (thing) toward (target) by (amount)", [this](Forge::CallContext& context)
			{
				Entity* thing = Resolve(context.NativeArg<EntityRef>(0, m_EntityType));
				Entity* target = Resolve(context.NativeArg<EntityRef>(1, m_EntityType));
				double amount = context.Number(2);
				double dx = target->X - thing->X;
				double dy = target->Y - thing->Y;
				double dz = target->Z - thing->Z;
				double distance = std::sqrt(dx * dx + dy * dy + dz * dz);
				if (distance > 0)
				{
					double step = std::min(amount, distance) / distance;
					thing->X += dx * step;
					thing->Y += dy * step;
					thing->Z += dz * step;
				}
				return Forge::Value();
			});

			m_VM.RegisterPhrase("find entity named (name)", [this](Forge::CallContext& context)
			{
				const std::string& name = context.Text(0);
				for (auto& entity : m_Scene.All())
				{
					if (entity->Alive && entity->Name == name)
						return Wrap(entity.get());
				}
				return Forge::Value();
			});
		}

	private:
		Scene& m_Scene;
		Forge::VM m_VM;
		Forge::NativeType m_EntityType;
	};

}

// ------------------------------------------------------------------ The scripts

static const char* s_EnemyScript = R"FORGE(
set target to find entity named "Hero"
set speed to 4

when update with delta
	if target is nothing then return end
	move self toward target by speed * delta
	if distance from my position to target.position is below 1 then
		say "{my name} reached {target.name}!"
		trigger attack with 25
	end
end

when attack with damage
	take damage from target.health
	say "{target.name} has {target.health} health left"
	if target.health is at most 0 then
		say "{target.name} was defeated"
		target.destroy()
		set target to nothing
	end
end
)FORGE";

static const char* s_HeroScript = R"FORGE(
when update with delta
	add 0.5 * delta to my position.y
end

when shouted with words
	say "{my name} hears: {words}"
	wait 1 second
	say "{my name} answers a second later"
end
)FORGE";

int main()
{
	Example::Scene scene;
	Example::ScriptEngine scripts(scene);

	Example::Entity* hero = scene.Create("Hero", 0);
	Example::Entity* goblin = scene.Create("Goblin", 10);
	hero->Health = 50;

	bool ok = scripts.AttachScript(hero, s_HeroScript, "hero.forge");
	ok = scripts.AttachScript(goblin, s_EnemyScript, "goblin.forge") && ok;
	if (!ok)
		return 1;

	scripts.SendEvent(hero, "shouted", { scripts.GetVM().MakeText("Watch out!") });

	// The game loop: 60 frames per second for 5 seconds.
	const double delta = 1.0 / 60.0;
	for (int frame = 0; frame < 300; frame++)
		scripts.Tick(delta);

	std::cout << "Hero alive: " << (hero->Alive ? "yes" : "no") << std::endl;
	return hero->Alive ? 1 : 0; // The example expects the goblin to win.
}
