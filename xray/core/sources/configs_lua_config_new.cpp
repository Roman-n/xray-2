#include "pch.h"
#include "cjson\cJSON.h"
#include "resources_manager.h"
#include "configs_lua_config_cook.h"
#include <xray\configs_lua_config.h>
#include <xray\strings_stream.h>

namespace xray {
	namespace core{
		namespace configs{
			extern memory::doug_lea_allocator g_lua_allocator;
		}
	}
	namespace configs {

		//main functions
		static void* json_malloc(size_t sz)
		{
			core::configs::g_lua_allocator.user_current_thread_id();
			return XRAY_MALLOC_IMPL(core::configs::g_lua_allocator, sz, "lua");
		}

		static void json_free(void* ptr)
		{
			core::configs::g_lua_allocator.user_current_thread_id();
			XRAY_FREE_IMPL(core::configs::g_lua_allocator, ptr);
		}

		void initialize()
		{
			cJSON_Hooks jh = {json_malloc,json_free};
			cJSON_InitHooks(&jh);
		}

		lua_config_ptr create_lua_config(pcstr file_name)
		{
			return create_lua_config(
				mutable_buffer(
					json_malloc(sizeof(lua_config)),
					sizeof(lua_config)),
				file_name);
		}

		lua_config_ptr create_lua_config(mutable_buffer const& buffer, pcstr file_name)
		{
			xray::core::configs::g_lua_allocator.user_current_thread_id();

			lua_config * result = new (buffer.c_ptr()) lua_config(file_name, cJSON_CreateObject());

			xray::resources::g_resources_manager->get_lua_config_cook().register_object_to_delete(result, threading::current_thread_id());
			result->set_creation_source(resources::resource_base::creation_source_created_by_user, file_name, buffer.size());

			return result;
		}

		lua_config_ptr create_lua_config_from_string(pcstr string)
		{
			pcstr file_name = ":from_clipboard";
			cJSON* cfg = cJSON_Parse(string);
			xray::mutable_buffer buffer(json_malloc(sizeof(lua_config)), sizeof(lua_config));

			if(!cfg)
			{
				const char *js = string, *err = cJSON_GetErrorPtr();
				u32 line = 1;

				for(; js < err; js++)
					if(*js == '\n') line++;

				LOG_ERROR("JSON '%s' parsing failed. Error at line %d", file_name, line);

				cfg = cJSON_CreateObject(); //avoid access violation
			}

			xray::core::configs::g_lua_allocator.user_current_thread_id();

			lua_config * result = new (buffer.c_ptr()) lua_config(file_name, cfg);

			xray::resources::g_resources_manager->get_lua_config_cook().register_object_to_delete(result, threading::current_thread_id());
			result->set_creation_source(resources::resource_base::creation_source_created_by_user,file_name,buffer.size());

			return result;
		}

		//lua config class implementation
		/*
		lua_config::lua_config(pcstr file_name, ::luabind::object const& object) : m_root(object)
		{
//			m_file_name = strdup(file_name);
			NOT_IMPLEMENTED();
		}
		*/
		lua_config::lua_config(pcstr file_name, cJSON* js) : m_root(js)
		{
			core::configs::g_lua_allocator.user_current_thread_id();
			m_file_name = file_name ? strings::duplicate(xray::core::configs::g_lua_allocator, file_name) : 0;
		}

		lua_config::~lua_config()
		{
			m_root.destroy();
			json_free(m_file_name);
		}

		void lua_config::save()
		{
			m_root.save(m_file_name);
		}

		void lua_config::save_as(pcstr file_name)
		{
			m_root.save(file_name);
		}

		void lua_config::save(xray::strings::stream &stream)
		{
			m_root.save(stream, 0, 0, 0);
		}


		//lua config value class implementation
		/*
		lua_config_value::lua_config_value(::luabind::object const&)
		{
			NOT_IMPLEMENTED();
//			root = cJSON_CreateObject();
		}
		*/
		lua_config_value::lua_config_value(cJSON* js) : root(js), parent(NULL)
		{
		}

		lua_config_value::lua_config_value(cJSON *js, cJSON *_parent) : root(js), parent(_parent)
		{
		}

		lua_config_value::~lua_config_value()
		{
		}

		void lua_config_value::save(xray::strings::stream& stream, pstr indent, u32 indent_size, u32 indent_level) const
		{
			XRAY_UNREFERENCED_PARAMETERS(indent, indent_size, indent_level);
			char *json = cJSON_Print(root);

			stream.append(json);

			json_free(json);
		}

		void lua_config_value::save(pcstr filepath)
		{
			FILE* file;
			char* json;

			bool result = fs::open_file(&file, fs::open_file_write | fs::open_file_create | fs::open_file_truncate, filepath);
			ASSERT_U(result);

			json = cJSON_Print(root);
			fwrite(json, strlen(json), 1, file);

			fclose(file);
			json_free(json);
		}

		void lua_config_value::clear()
		{
			cJSON_Delete(root->child);
			root->child = 0;
		}

		bool lua_config_value::value_exists(pcstr name) const
		{
			return cJSON_GetObjectItem(root, name) != 0;
		}

		void lua_config_value::destroy()
		{
			cJSON_Delete(root);
		}

		lua_config_value::lua_config_value(lua_config_value const& other)
		{
			root = other.root;
			parent = other.parent;
		}

		lua_config_value& lua_config_value::operator = (lua_config_value const& other)
		{
			cJSON* item = cJSON_Duplicate(other.root, 1);
//			*root = *item;

			cJSON_Delete(root->child);
			json_free(root->valuestring);

			root->child = item->child;
			root->type = item->type;
			root->valuedouble = item->valuedouble;
			root->valueint = item->valueint;
			root->valuestring = item->valuestring;

			//cleanup
			json_free(item->string);
			json_free(item);

			return *this;
		}

		lua_config_value& lua_config_value::assign_lua_value(lua_config_value const& other)
		{
			cJSON* item = cJSON_Duplicate(other.root, 1);
//			*root = *item;

			cJSON_Delete(root->child);
			json_free(root->valuestring);

			root->child = item->child;
			root->type = item->type;
			root->valuedouble = item->valuedouble;
			root->valueint = item->valueint;
			root->valuestring = item->valuestring;

			//cleanup
			json_free(item->string);
			json_free(item);

			return *this;
		}

		void lua_config_value::swap(lua_config_value & other)
		{
			cJSON temp = *other.root;

			*other.root = *root;
			*root = temp;
		}

		lua_config_iterator lua_config_value::begin()
		{
			if(root->type != cJSON_Object && root->type != cJSON_Array)
				LOG_ERROR("trying create iterator for non-array value");

			return lua_config_iterator(root->child, root);
		}

		lua_config_iterator lua_config_value::end()
		{
			if(root->type != cJSON_Object && root->type != cJSON_Array)
				LOG_ERROR("trying create iterator for non-array value");

//			return lua_config_iterator(cJSON_GetArrayItem(root,cJSON_GetArraySize(root)-1));
			//будет перебирать пока child->next != 0
			return lua_config_iterator((cJSON*)0, root);
		}

		lua_config_iterator lua_config_value::begin() const
		{
			if(root->type != cJSON_Object && root->type != cJSON_Array)
				LOG_ERROR("trying create iterator for non-array value");

			return lua_config_iterator(root->child, root);
		}

		lua_config_iterator lua_config_value::end() const
		{
			if(root->type != cJSON_Object && root->type != cJSON_Array)
				LOG_ERROR("trying create iterator for non-array value");

//			return lua_config_iterator(cJSON_GetArrayItem(root,cJSON_GetArraySize(root)-1));
			//будет перебирать пока child->next != 0
			return lua_config_iterator((cJSON*)0, root);
		}

		bool lua_config_value::empty() const
		{
			return cJSON_GetArraySize(root) == 0;
		}

		u32 lua_config_value::size() const
		{
			return cJSON_GetArraySize(root);
		}

		lua_config_value const lua_config_value::operator [] (u32 id) const
		{
			if(root->type == cJSON_Array || root->type == cJSON_Object)
			{
				if(id >= cJSON_GetArraySize(root))
				{
					cJSON* item = cJSON_CreateNull();
					cJSON_AddItemToArray(root,item);

					return lua_config_value(item, root);
				}
				else
					return lua_config_value(cJSON_GetArrayItem(root, id), root);
			}
			else
			{
				root->type = cJSON_Array;

				cJSON* item = cJSON_CreateNull();
				cJSON_AddItemToArray(root, item);

				return lua_config_value(item, root);
			}
		}

		lua_config_value const lua_config_value::operator [] (int id) const
		{
			if(root->type == cJSON_Array || root->type == cJSON_Object)
			{
				if(id >= cJSON_GetArraySize(root))
				{
					cJSON* item = cJSON_CreateNull();
					cJSON_AddItemToArray(root, item);

					return lua_config_value(item, root);
				}
				else
					return lua_config_value(cJSON_GetArrayItem(root, id), root);
			}
			else
			{
				root->type = cJSON_Array;

				cJSON* item = cJSON_CreateNull();
				cJSON_AddItemToArray(root, item);

				return lua_config_value(item, root);
			}
		}

		lua_config_value const lua_config_value::operator [] (pcstr name) const
		{
			if(value_exists(name))
				return lua_config_value(cJSON_GetObjectItem(root, name), root);
			else
			{
				cJSON* item = cJSON_CreateNull();

				root->type = cJSON_Object;
				cJSON_AddItemToObject(root, name, item);

				return lua_config_value(item, root);
			}
		}

		lua_config_value lua_config_value::operator [] (u32 id)
		{
			if(root->type == cJSON_Array || root->type == cJSON_Object)
			{
				if(id >= cJSON_GetArraySize(root))
				{
					cJSON* item = cJSON_CreateNull();
					cJSON_AddItemToArray(root, item);

					return lua_config_value(item);
				}
				else
					return lua_config_value(cJSON_GetArrayItem(root, id), root);
			}
			else
			{
				root->type = cJSON_Array;

				cJSON* obj = cJSON_CreateNull();
				cJSON_AddItemToArray(root, obj);

				return lua_config_value(obj, root);
			}
		}

		lua_config_value lua_config_value::operator [] (int id)
		{
			if(root->type == cJSON_Array || root->type == cJSON_Object)
			{
				if(id >= cJSON_GetArraySize(root))
				{
					cJSON* item = cJSON_CreateNull();
					cJSON_AddItemToArray(root, item);

					return lua_config_value(item, root);
				}
				else
					return lua_config_value(cJSON_GetArrayItem(root, id), root);
			}
			else
			{
				root->type = cJSON_Array;

				cJSON* obj = cJSON_CreateNull();
				cJSON_AddItemToArray(root, obj);

				return lua_config_value(obj, root);
			}
		}

		lua_config_value lua_config_value::operator [] (pcstr name)
		{
			if(value_exists(name))
				return lua_config_value(cJSON_GetObjectItem(root, name), root);
			else
			{
				cJSON* item = cJSON_CreateNull();

				root->type = cJSON_Object;
				cJSON_AddItemToObject(root, name, item);

				return lua_config_value(item, root);
			}
		}

		lua_config_value::operator bool () const
		{
			if(root->type == cJSON_True || root->type == cJSON_False)
				return !!root->type;
			else if(root->type == cJSON_Number)
				return !!root->valueint;
			else
				return false;
		}

		lua_config_value::operator s8 () const
		{
			return (s8)root->valueint;
		}

		lua_config_value::operator u8 () const
		{
			return (u8)root->valueint;
		}

		lua_config_value::operator s16 () const
		{
			return (s16)root->valueint;
		}

		lua_config_value::operator u16 () const
		{
			return (u16)root->valueint;
		}

		lua_config_value::operator s32 () const
		{
			return (s32)root->valueint;
		}

		lua_config_value::operator u32 () const
		{
			return (u32)root->valueint;
		}

		lua_config_value::operator float () const
		{
			return (float)root->valuedouble;
		}

		lua_config_value::operator pcstr () const
		{
			if(root->type == cJSON_String)
				return root->valuestring;
			else
			{
				//memory leak..
				core::configs::g_lua_allocator.user_current_thread_id();
				return strings::duplicate(core::configs::g_lua_allocator, "nil");
			}
		}

		lua_config_value::operator xray::math::float2 () const
		{
			if(root->type != cJSON_String)
				return xray::math::float2(0.0, 0.0);

			xray::math::float2 val(0.0, 0.0);
			u32 num = XRAY_SSCANF(root->valuestring, "%f, %f", &val.x, &val.y);

			if(num != 2)
				LOG_WARNING("invalid float2 value. %s", root->string ? root->string : "");

			return val;
		}

		lua_config_value::operator xray::math::float3 () const
		{
			if(root->type != cJSON_String)
				return xray::math::float3(0.0, 0.0, 0.0);

			xray::math::float3 val(0.0, 0.0, 0.0);
			u32 num = XRAY_SSCANF(root->valuestring, "%f, %f, %f", &val.x, &val.y, &val.z);

			if(num != 3)
				LOG_WARNING("invalid float3 value. %s", root->string ? root->string : "");

			return val;
		}

		lua_config_value::operator xray::math::float4 () const
		{
			if(root->type != cJSON_String)
				return xray::math::float4(0.0, 0.0, 0.0, 0.0);

			xray::math::float4 val(0.0, 0.0, 0.0, 0.0);
			u32 num = XRAY_SSCANF(root->valuestring, "%f, %f, %f, %f", &val.x, &val.y, &val.z, &val.w);

			if(num != 4)
				LOG_WARNING("invalid float4 value. %s", root->string ? root->string : "");

			return val;
		}

		lua_config_value lua_config_value::operator = (pcstr str)
		{
			if(root->valuestring) 
				XRAY_FREE_IMPL(core::configs::g_lua_allocator, root->valuestring);

			root->type = cJSON_String;
			root->valuestring = strings::duplicate(core::configs::g_lua_allocator, str);

			return *this;
		}

		lua_config_value lua_config_value::operator = (bool b)
		{
			if(b)
				root->type = root->valueint = cJSON_True;
			else
				root->type = root->valueint = cJSON_False;

			return *this;
		}

		lua_config_value lua_config_value::operator = (int num)
		{
			root->type = cJSON_Number;
			root->valueint = num;
			root->valuedouble = (double)num;

			return *this;
		}

		lua_config_value lua_config_value::operator = (u32 num)
		{
			root->type = cJSON_Number;
			root->valueint = num;
			root->valuedouble = (double)num;

			return *this;
		}

		lua_config_value lua_config_value::operator = (float num)
		{
			root->type = cJSON_Number;
			root->valueint = (int)num;
			root->valuedouble = num;

			return *this;
		}

		lua_config_value lua_config_value::operator = (math::float2 const& value)
		{
			char buf[128];
			root->type = cJSON_String;
			XRAY_FREE_IMPL(core::configs::g_lua_allocator,root->valuestring);

			sprintf(buf,sizeof(buf),"%f, %f", value.x, value.y);
			root->valuestring = strings::duplicate(core::configs::g_lua_allocator,buf);

			return *this;
		}

		lua_config_value lua_config_value::operator = (math::float3 const& value)
		{
			char buf[128];
			root->type = cJSON_String;
			XRAY_FREE_IMPL(core::configs::g_lua_allocator,root->valuestring);

			sprintf(buf,sizeof(buf),"%f, %f, %f",value.x,value.y,value.z);
			root->valuestring = strings::duplicate(core::configs::g_lua_allocator,buf);

			return *this;
		}

		lua_config_value lua_config_value::operator = (math::float4 const& value)
		{
			char buf[128];
			root->type = cJSON_String;
			XRAY_FREE_IMPL(core::configs::g_lua_allocator,root->valuestring);

			sprintf(buf,sizeof(buf),"%f, %f, %f, %f",value.x,value.y,value.z,value.w);
			root->valuestring = strings::duplicate(core::configs::g_lua_allocator,buf);

			return *this;
		}

		lua_config_value lua_config_value::create_table()
		{
			root->type = cJSON_Object;

			return *this;
		}

		void lua_config_value::erase(pcstr name)
		{
			cJSON_DeleteItemFromObject(root, name);
		}

		void lua_config_value::remove_from_parent()
		{
			ASSERT(parent != NULL, "you cannot remove root of config");

			if(parent->type == cJSON_Object)
			{
				cJSON_DeleteItemFromObject(parent, root->string);
			}
			else if(parent->type == cJSON_Array)
			{
				int which;
				cJSON *c;

				which = 0;
				c = parent->child;

				while(c)
				{
					if(c == root)
					{
						cJSON_DeleteItemFromArray(parent, which);
						return;
					}

					c = c->next;
					which++;
				}

				UNREACHABLE_CODE();
			}
			else
				UNREACHABLE_CODE();
		}

		void lua_config_value::rename(pcstr new_name)
		{
			ASSERT(!value_exists(new_name));

			XRAY_FREE_IMPL(core::configs::g_lua_allocator, root->string);
			root->string = strings::duplicate(core::configs::g_lua_allocator,new_name);
		}

		void lua_config_value::add_super_table(lua_config_value const& table)
		{
			DEBUG_BREAK();

			cJSON *item = cJSON_Duplicate(table.root,1);
			cJSON_AddItemToObject(root,item->string,item);
		}

		void lua_config_value::add_super_table(pcstr file, pcstr name)
		{
			FILE *f;
			pcstr file_name = 0;
			char *raw;
			cJSON *json,*dup;

			return;
			DEBUG_BREAK();

			//очень-очень-очень грубый костыль.
			//Однако, ф-ция используется только в одном месте и я не знаю как по другому реализовать.
			//Наверное лучше это вообще заменить.
			STR_JOINA(file_name,file,".library");

			fs::open_file(&f,fs::open_file_read,file_name);

			raw = (char*)json_malloc(_filelength(fileno(f)));
			fread(raw,_filelength((int)f),1,f);

			json = cJSON_Parse(raw);

			if(json == NULL)
				LOG_ERROR("JSON parsing failed! %s",file_name);

			dup = cJSON_Duplicate(json,1);
			cJSON_AddItemToObject(root,name,dup);

			fclose(f);
			cJSON_Delete(json);
			json_free(raw);
		}

		void lua_config_value::remove_super_table(lua_config_value const& table)
		{
			DEBUG_BREAK();
			cJSON_DeleteItemFromObject(root,table.root->string);
		}

		void lua_config_value::remove_super_table(pcstr file, pcstr name)
		{
			DEBUG_BREAK();
			cJSON_DeleteItemFromObject(root,name);
		}

		enum_types lua_config_value::get_type() const
		{
			switch(root->type)
			{
				case cJSON_False:
				case cJSON_True:
					return t_boolean; 
				break;

				case cJSON_Number:
				{
					if(root->valuedouble - root->valueint == 0.0)
						return t_integer;
					else
						return t_float;
				} 
				break;

				case cJSON_String:
					return t_string; 
				break;

				case cJSON_Object:
					return t_table_named; 
				break;

				case cJSON_Array:
					return t_table_indexed; 
				break;

				case cJSON_NULL:
					return t_integer; 
				break;

				default:
					UNREACHABLE_CODE();
			}
		}
		/*
		luabind::object const& lua_config_value::get_object() const
		{
			NOT_IMPLEMENTED();
			return *m_object;
		}
		*/
		pcstr lua_config_value::get_field_id() const
		{
			return root->string;
		}

		//lua config iterator class implemetation
		lua_config_iterator::lua_config_iterator(const luabind::iterator&)
		{
			NOT_IMPLEMENTED();
		}

		lua_config_iterator::lua_config_iterator(cJSON* js, cJSON* _parent) : root(js), parent(_parent)
		{
		}

		lua_config_iterator::lua_config_iterator(const lua_config_iterator& other)
		{
			root = other.root;
		}

		lua_config_iterator::~lua_config_iterator()
		{
		}

		lua_config_iterator& lua_config_iterator::operator = (lua_config_iterator const& other)
		{
			root = other.root;
			return *this;
		}

		lua_config_value lua_config_iterator::key() const
		{
			// memory leaks here

			
			if(!parent)
			{
				return lua_config_value(cJSON_CreateString("ROOT"));
			}
			else if(parent->type == cJSON_Object)
			{
				return lua_config_value(cJSON_CreateString(root->string));
			}
			else if(parent->type == cJSON_Array)
			{
				cJSON *c = parent->child;
				int which;

				char str[16];

				for(which = 0; c; c = c->next, which++)
				{
					if(c == root)
						break;
				}

				sprintf(str, sizeof(str), "%d", which);

				return lua_config_value(cJSON_CreateString(str));
			}
			else
			{
				return lua_config_value(cJSON_CreateString("nil"));
			}
		}

		lua_config_value lua_config_iterator::operator *() const
		{
			return lua_config_value(root, parent);
		}

		bool lua_config_iterator::operator == (lua_config_iterator const& other) const
		{
			return root == other.root;
		}

		bool lua_config_iterator::operator != (lua_config_iterator const& other) const
		{
			return root != other.root;
		}

		lua_config_iterator& lua_config_iterator::operator ++ ()
		{
			if(root)
				root = root->next;

			return *this;
		}

		lua_config_iterator lua_config_iterator::operator ++ (int)
		{
			lua_config_iterator result = *this;
			
			if(root)
				root = root->next;

			return result;
		}
	}
}