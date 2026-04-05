#pragma once
#ifndef ES_APP_COMPONENTS_CHARACTER_ROW_COMPONENT_H
#define ES_APP_COMPONENTS_CHARACTER_ROW_COMPONENT_H

#include "GuiComponent.h"
#include "resources/Font.h"
#include <functional>
#include <string>
#include <vector>

class CharacterRowComponent : public GuiComponent
{
public:
	CharacterRowComponent(Window* window);

	bool input(InputConfig* config, Input input) override;
	void render(const Transform4x4f& parentTrans) override;

	void setCharSelectedCallback(const std::function<void(const std::string&)>& cb) { mCharSelectedCb = cb; }
	void setBackspaceCallback(const std::function<void()>& cb) { mBackspaceCb = cb; }

	enum Mode { LETTERS, NUMBERS, SYMBOLS };
	Mode getMode() const { return mMode; }

	void setFont(const std::shared_ptr<Font>& font) { mFont = font; }
	void setSelectorColor(unsigned int color) { mSelectorColor = color; }
	void setTextColor(unsigned int color) { mTextColor = color; }

	int getCursor() const { return mCursor; }

private:
	void buildCharList();

	Mode mMode;
	int mCursor;
	std::vector<std::string> mChars;

	std::function<void(const std::string&)> mCharSelectedCb;
	std::function<void()> mBackspaceCb;

	std::shared_ptr<Font> mFont;
	unsigned int mSelectorColor;
	unsigned int mTextColor;

	static const std::string MODE_SWITCH_123;
	static const std::string MODE_SWITCH_ABC;
	static const std::string MODE_SWITCH_SYMBOLS;
	static const std::string CHAR_SPACE;
	static const std::string CHAR_BACKSPACE;
};

#endif // ES_APP_COMPONENTS_CHARACTER_ROW_COMPONENT_H
