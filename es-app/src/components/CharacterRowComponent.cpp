#include "components/CharacterRowComponent.h"

#include "renderers/Renderer.h"
#include "InputConfig.h"

const std::string CharacterRowComponent::MODE_SWITCH_123     = "123";
const std::string CharacterRowComponent::MODE_SWITCH_ABC     = "ABC";
const std::string CharacterRowComponent::MODE_SWITCH_SYMBOLS = "!@#";
const std::string CharacterRowComponent::CHAR_SPACE          = "SPC";
const std::string CharacterRowComponent::CHAR_BACKSPACE      = "\xe2\x8c\xab"; // ⌫ U+232B
const std::string CharacterRowComponent::CHAR_CURSOR_LEFT    = "\xe2\x86\x90"; // ← U+2190
const std::string CharacterRowComponent::CHAR_CURSOR_RIGHT   = "\xe2\x86\x92"; // → U+2192

CharacterRowComponent::CharacterRowComponent(Window* window)
	: GuiComponent(window), mMode(LETTERS), mCursor(2), mFocused(true), mSelectorColor(0x000050FF), mTextColor(0xFFFFFFFF)
{
	mFont = Font::get(FONT_SIZE_MEDIUM);
	buildCharList();
}

void CharacterRowComponent::buildCharList()
{
	mChars.clear();

	switch (mMode)
	{
	case LETTERS:
		mChars.push_back(MODE_SWITCH_123);
		mChars.push_back(CHAR_SPACE);
		for (char c = 'A'; c <= 'Z'; c++)
			mChars.push_back(std::string(1, c));
		mChars.push_back(CHAR_BACKSPACE);
		mChars.push_back(CHAR_CURSOR_LEFT);
		mChars.push_back(CHAR_CURSOR_RIGHT);
		break;

	case NUMBERS:
		mChars.push_back(MODE_SWITCH_SYMBOLS);
		mChars.push_back(CHAR_SPACE);
		for (char c = '1'; c <= '9'; c++)
			mChars.push_back(std::string(1, c));
		mChars.push_back("0");
		mChars.push_back(CHAR_BACKSPACE);
		mChars.push_back(CHAR_CURSOR_LEFT);
		mChars.push_back(CHAR_CURSOR_RIGHT);
		break;

	case SYMBOLS:
		mChars.push_back(MODE_SWITCH_ABC);
		mChars.push_back(CHAR_SPACE);
		{
			const char* syms = "!@#$%^&*()-_+=";
			for (int i = 0; syms[i]; i++)
				mChars.push_back(std::string(1, syms[i]));
		}
		mChars.push_back(CHAR_BACKSPACE);
		mChars.push_back(CHAR_CURSOR_LEFT);
		mChars.push_back(CHAR_CURSOR_RIGHT);
		break;
	}

	if (mCursor >= (int)mChars.size())
		mCursor = (int)mChars.size() - 1;
}

bool CharacterRowComponent::input(InputConfig* config, Input input)
{
	if (input.value != 0)
	{
		if (config->isMappedLike("left", input))
		{
			if (mCursor > 0)
				mCursor--;
			else
				mCursor = (int)mChars.size() - 1;
			return true;
		}
		else if (config->isMappedLike("right", input))
		{
			if (mCursor < (int)mChars.size() - 1)
				mCursor++;
			else
				mCursor = 0;
			return true;
		}
		else if (config->isMappedTo("a", input))
		{
			const std::string& selected = mChars[mCursor];

			if (selected == MODE_SWITCH_123)
			{
				mMode = NUMBERS;
				buildCharList();
			}
			else if (selected == MODE_SWITCH_SYMBOLS)
			{
				mMode = SYMBOLS;
				buildCharList();
			}
			else if (selected == MODE_SWITCH_ABC)
			{
				mMode = LETTERS;
				buildCharList();
			}
			else if (selected == CHAR_BACKSPACE)
			{
				if (mBackspaceCb)
					mBackspaceCb();
			}
			else if (selected == CHAR_CURSOR_LEFT)
			{
				if (mCursorLeftCb)
					mCursorLeftCb();
			}
			else if (selected == CHAR_CURSOR_RIGHT)
			{
				if (mCursorRightCb)
					mCursorRightCb();
			}
			else if (selected == CHAR_SPACE)
			{
				if (mCharSelectedCb)
					mCharSelectedCb(" ");
			}
			else
			{
				if (mCharSelectedCb)
					mCharSelectedCb(selected);
			}
			return true;
		}
	}

	return GuiComponent::input(config, input);
}

void CharacterRowComponent::render(const Transform4x4f& parentTrans)
{
	Transform4x4f trans = parentTrans * getTransform();

	if (mChars.empty())
		return;

	const float height = mSize.y();
	const float padding = Math::round(height * 0.2f);

	// Calculate total width needed to center the row
	float totalWidth = 0;
	std::vector<float> charWidths;
	for (const auto& ch : mChars)
	{
		float w = mFont->sizeText(ch).x() + padding * 2;
		charWidths.push_back(w);
		totalWidth += w;
	}

	// Start x position - center the row
	float startX = Math::round((mSize.x() - totalWidth) / 2.0f);
	if (startX < 0)
		startX = 0;

	float x = startX;
	const float textY = Math::round((height - mFont->getHeight(1.0f)) / 2.0f);

	Renderer::setMatrix(trans);

	for (int i = 0; i < (int)mChars.size(); i++)
	{
		float cellWidth = charWidths[i];

		// Draw selector background for current item (only when focused)
		if (mFocused && i == mCursor)
		{
			Renderer::drawRect(Math::round(x), 0, Math::round(cellWidth), Math::round(height),
				mSelectorColor, mSelectorColor);
		}

		// Build and render text
		auto textCache = std::unique_ptr<TextCache>(
			mFont->buildTextCache(mChars[i], Math::round(x + padding), Math::round(textY),
				(i == mCursor) ? 0xFFFFFFFF : mTextColor));
		mFont->renderTextCache(textCache.get());

		x += cellWidth;
	}

	renderChildren(trans);
}
