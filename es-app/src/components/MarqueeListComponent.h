#pragma once
#ifndef ES_APP_COMPONENTS_MARQUEE_LIST_COMPONENT_H
#define ES_APP_COMPONENTS_MARQUEE_LIST_COMPONENT_H

#include "Log.h"
#include "animations/LambdaAnimation.h"
#include "components/IList.h"
#include "components/ImageComponent.h"
#include "resources/Font.h"
#include "resources/TextureResource.h"
#include "Sound.h"

struct MarqueeListData
{
	std::string texturePath;
	std::shared_ptr<TextureResource> texture;
	bool textureReady;  // true once updateTextureSize() reports loaded/failed
};

template<typename T>
class MarqueeListComponent : public IList<MarqueeListData, T>
{
protected:
	using IList<MarqueeListData, T>::mEntries;
	using IList<MarqueeListData, T>::mScrollTier;
	using IList<MarqueeListData, T>::listUpdate;
	using IList<MarqueeListData, T>::listInput;
	using IList<MarqueeListData, T>::listRenderTitleOverlay;
	using IList<MarqueeListData, T>::getTransform;
	using IList<MarqueeListData, T>::mSize;
	using IList<MarqueeListData, T>::mCursor;
	using IList<MarqueeListData, T>::mWindow;

public:
	using IList<MarqueeListData, T>::size;
	using IList<MarqueeListData, T>::isScrolling;
	using IList<MarqueeListData, T>::stopScrolling;

	MarqueeListComponent(Window* window);

	void add(const std::string& name, const std::string& marqueePath, const T& obj);

	bool input(InputConfig* config, Input input) override;
	void update(int deltaTime) override;
	void render(const Transform4x4f& parentTrans) override;
	virtual void applyTheme(const std::shared_ptr<ThemeData>& theme, const std::string& view, const std::string& element, unsigned int properties) override;

	void onSizeChanged() override;
	inline void setCursorChangedCallback(const std::function<void(CursorState state)>& func) { mCursorChangedCallback = func; }

protected:
	virtual void onScroll(int /*amt*/) override { if (!mScrollSound.empty()) Sound::get(mScrollSound)->play(); }
	virtual void onCursorChanged(const CursorState& state) override;

private:
	void updateTextures();
	void renderMarqueeItem(const Transform4x4f& trans, int entryIndex, float yPos, bool selected);

	// Smooth scroll camera
	float mCamera;           // 0..1 animation progress for smooth scroll
	float mCameraDirection;  // -1 or 1
	int mLastCursor;

	// Theme properties
	float mItemHeight;       // fraction of component height per slot
	float mItemSpacing;      // fraction of component height for gap
	float mSelectedScale;    // scale multiplier for selected
	float mScrollAnimDuration; // ms for scroll animation
	int mMaxVisible;         // how many items visible (odd, selected centered)
	std::string mDefaultImage;
	unsigned int mTextFallbackColor;
	std::shared_ptr<Font> mTextFallbackFont;
	std::string mScrollSound;
	int mLoadRadius;         // how many entries around cursor to keep loaded

	std::function<void(CursorState state)> mCursorChangedCallback;
};

// ============================================================================
// Implementation
// ============================================================================

template<typename T>
MarqueeListComponent<T>::MarqueeListComponent(Window* window) : IList<MarqueeListData, T>(window)
{
	mCamera = 0.0f;
	mCameraDirection = 1.0f;
	mLastCursor = -1;

	mItemHeight = 0.12f;
	mItemSpacing = 0.02f;
	mSelectedScale = 1.3f;
	mScrollAnimDuration = 150.0f;
	mMaxVisible = 7;
	mTextFallbackColor = 0x000000FF;
	mTextFallbackFont = Font::get(FONT_SIZE_MEDIUM);
	mLoadRadius = 5; // entries beyond visible to keep loaded

	mSize = Vector2f((float)Renderer::getScreenWidth() * 0.48f, (float)Renderer::getScreenHeight());
}

template<typename T>
void MarqueeListComponent<T>::add(const std::string& name, const std::string& marqueePath, const T& obj)
{
	typename IList<MarqueeListData, T>::Entry entry;
	entry.name = name;
	entry.object = obj;
	entry.data.texturePath = marqueePath;
	entry.data.texture = nullptr;
	entry.data.textureReady = false;

	static_cast<IList<MarqueeListData, T>*>(this)->add(entry);
}

template<typename T>
bool MarqueeListComponent<T>::input(InputConfig* config, Input input)
{
	if (size() > 0)
	{
		bool isSingleStep = config->isMappedLike("down", input) || config->isMappedLike("up", input);
		bool isPageStep = config->isMappedLike("rightshoulder", input) || config->isMappedLike("leftshoulder", input);

		if (isSingleStep || isPageStep)
		{
			if (input.value != 0)
			{
				int delta;
				if (isSingleStep)
					delta = config->isMappedLike("down", input) ? 1 : -1;
				else
				{
					delta = 10;
					if (config->isMappedLike("leftshoulder", input))
						delta = -delta;
				}
				listInput(delta);
				return true;
			}
			else
			{
				stopScrolling();
			}
		}
	}

	return GuiComponent::input(config, input);
}

template<typename T>
void MarqueeListComponent<T>::update(int deltaTime)
{
	GuiComponent::update(deltaTime);
	listUpdate(deltaTime);
	updateTextures();
}

template<typename T>
void MarqueeListComponent<T>::updateTextures()
{
	if (size() == 0)
		return;

	int loadRange = mMaxVisible / 2 + mLoadRadius;

	// At tier 3 (fastest scroll), skip loading new textures
	bool skipNewLoads = (mScrollTier >= 3);

	for (int i = 0; i < (int)mEntries.size(); i++)
	{
		auto& entry = mEntries[i];
		int dist = std::abs(i - mCursor);
		// Handle wrap-around distance
		int wrapDist = (int)mEntries.size() - dist;
		if (wrapDist < dist)
			dist = wrapDist;

		if (dist <= loadRange)
		{
			// Load texture if not loaded and path is non-empty
			if (!entry.data.texture && !entry.data.texturePath.empty() && !skipNewLoads)
			{
				entry.data.texture = TextureResource::get(entry.data.texturePath, false, false, true, false);
				entry.data.textureReady = false;
			}

			// Poll async loading progress for textures that are loading
			if (entry.data.texture && !entry.data.textureReady)
			{
				if (entry.data.texture->updateTextureSize())
				{
					entry.data.textureReady = true;
				}
			}
		}
		else
		{
			// Unload distant textures
			if (entry.data.texture)
			{
				entry.data.texture.reset();
				entry.data.textureReady = false;
			}
		}
	}
}

template<typename T>
void MarqueeListComponent<T>::render(const Transform4x4f& parentTrans)
{
	if (size() == 0)
		return;

	Transform4x4f trans = parentTrans * getTransform();

	// Set up clip rect
	float scaleX = trans.r0().x();
	float scaleY = trans.r1().y();
	Vector2i pos((int)Math::round(trans.translation()[0]), (int)Math::round(trans.translation()[1]));
	Vector2i clipSize((int)Math::round(mSize.x() * scaleX), (int)Math::round(mSize.y() * scaleY));
	Renderer::pushClipRect(pos, clipSize);

	float slotH = mSize.y() * mItemHeight;
	float gapH = mSize.y() * mItemSpacing;
	float stride = slotH + gapH;

	// Center of component
	float centerY = mSize.y() * 0.5f;

	// Camera offset for smooth scrolling
	float cameraOffset = mCamera * mCameraDirection * stride;

	// Render items around the cursor
	int halfVisible = mMaxVisible / 2;

	// Render non-selected items first, then selected on top
	for (int pass = 0; pass < 2; pass++)
	{
		for (int offset = -halfVisible - 1; offset <= halfVisible + 1; offset++)
		{
			int entryIdx = mCursor + offset;
			// Wrap around
			if (entryIdx < 0) entryIdx += size();
			if (entryIdx >= size()) entryIdx -= size();
			// Clamp for non-wrapping (pause at end list)
			if (entryIdx < 0 || entryIdx >= size())
				continue;

			bool isSelected = (offset == 0);
			if ((pass == 0 && isSelected) || (pass == 1 && !isSelected))
				continue;

			float yPos = centerY + offset * stride + cameraOffset - slotH * 0.5f;

			// Skip if fully outside clip bounds
			if (yPos + slotH * mSelectedScale < 0 || yPos > mSize.y())
				continue;

			renderMarqueeItem(trans, entryIdx, yPos, isSelected);
		}
	}

	Renderer::popClipRect();

	listRenderTitleOverlay(trans);
	GuiComponent::renderChildren(trans);
}

template<typename T>
void MarqueeListComponent<T>::renderMarqueeItem(const Transform4x4f& trans, int entryIndex, float yPos, bool selected)
{
	auto& entry = mEntries.at(entryIndex);

	float slotH = mSize.y() * mItemHeight;
	float slotW = mSize.x();
	float scale = selected ? mSelectedScale : 1.0f;

	float scaledH = slotH * scale;
	float scaledW = slotW * scale;

	// Center the scaled item vertically in its slot
	float drawY = yPos + (slotH - scaledH) * 0.5f;
	float drawX = (slotW - scaledW) * 0.5f;

	bool textureRendered = false;
	if (entry.data.texture && entry.data.textureReady)
	{
		auto texSize = entry.data.texture->getSize();
		if (texSize.x() > 0 && texSize.y() > 0)
		{
			float imgAspect = (float)texSize.x() / (float)texSize.y();
			float slotAspect = scaledW / scaledH;

			float renderW, renderH;
			if (imgAspect > slotAspect)
			{
				renderW = scaledW;
				renderH = scaledW / imgAspect;
			}
			else
			{
				renderH = scaledH;
				renderW = scaledH * imgAspect;
			}

			float imgX = drawX + (scaledW - renderW) * 0.5f;
			float imgY = drawY + (scaledH - renderH) * 0.5f;

			// Round positions to avoid sub-pixel artifacts
			imgX = Math::round(imgX);
			imgY = Math::round(imgY);
			renderW = Math::round(renderW);
			renderH = Math::round(renderH);

			// UV convention: V=1 at top, V=0 at bottom (ES OpenGL convention)
			Renderer::Vertex vertices[4];
			vertices[0] = { { imgX,            imgY             }, { 0.0f, 1.0f }, 0xFFFFFFFF };
			vertices[1] = { { imgX,            imgY + renderH   }, { 0.0f, 0.0f }, 0xFFFFFFFF };
			vertices[2] = { { imgX + renderW,  imgY             }, { 1.0f, 1.0f }, 0xFFFFFFFF };
			vertices[3] = { { imgX + renderW,  imgY + renderH   }, { 1.0f, 0.0f }, 0xFFFFFFFF };

			Renderer::setMatrix(trans);
			if (entry.data.texture->bind())
			{
				Renderer::drawTriangleStrips(&vertices[0], 4);
				textureRendered = true;
			}
		}
	}

	if (!textureRendered)
	{
		// Fallback: render game name as text
		float textY = drawY + (scaledH - mTextFallbackFont->getHeight()) * 0.5f;
		float textX = drawX;

		Vector2f textSize = mTextFallbackFont->sizeText(entry.name);
		if (textSize.x() <= scaledW)
		{
			textX = drawX + (scaledW - textSize.x()) * 0.5f;
		}

		textX = Math::round(textX);
		textY = Math::round(textY);

		Renderer::setMatrix(trans);
		TextCache* cache = mTextFallbackFont->buildTextCache(entry.name, textX, textY, mTextFallbackColor);
		mTextFallbackFont->renderTextCache(cache);
		delete cache;
	}
}

template<typename T>
void MarqueeListComponent<T>::onSizeChanged()
{
	// Recalculate anything size-dependent
}

template<typename T>
void MarqueeListComponent<T>::onCursorChanged(const CursorState& state)
{
	if (mLastCursor == mCursor)
	{
		if (state == CURSOR_STOPPED && mCursorChangedCallback)
			mCursorChangedCallback(state);
		return;
	}

	bool direction = mCursor >= mLastCursor;
	int diff = direction ? mCursor - mLastCursor : mLastCursor - mCursor;
	if (diff == (int)mEntries.size() - 1)
		direction = !direction;

	mCameraDirection = direction ? -1.0f : 1.0f;

	int prevCursor = mLastCursor;
	mLastCursor = mCursor;

	if (mCursorChangedCallback)
		mCursorChangedCallback(state);

	// Cancel any existing scroll animation
	if (((GuiComponent*)this)->isAnimationPlaying(2))
		((GuiComponent*)this)->cancelAnimation(2);

	// At high scroll tiers, shorten or skip animation
	float animDuration = mScrollAnimDuration;
	if (mScrollTier >= 2)
		animDuration = 50.0f;
	if (mScrollTier >= 3)
		animDuration = 0.0f;

	if (prevCursor < 0 || animDuration <= 0.0f)
	{
		mCamera = 0.0f;
		return;
	}

	mCamera = 0.0f;

	auto func = [this](float t)
	{
		// Cubic ease out
		t -= 1;
		float pct = t * t * t + 1;
		mCamera = 1.0f - pct;
	};

	((GuiComponent*)this)->setAnimation(new LambdaAnimation(func, (int)animDuration), 0, [this] {
		mCamera = 0.0f;
	}, false, 2);
}

template<typename T>
void MarqueeListComponent<T>::applyTheme(const std::shared_ptr<ThemeData>& theme, const std::string& view, const std::string& element, unsigned int properties)
{
	GuiComponent::applyTheme(theme, view, element, properties);

	const ThemeData::ThemeElement* elem = theme->getElement(view, element, "marqueelist");
	if (elem)
	{
		if (elem->has("itemHeight"))
			mItemHeight = elem->get<float>("itemHeight");

		if (elem->has("itemSpacing"))
			mItemSpacing = elem->get<float>("itemSpacing");

		if (elem->has("selectedScale"))
			mSelectedScale = elem->get<float>("selectedScale");

		if (elem->has("scrollAnimDuration"))
			mScrollAnimDuration = elem->get<float>("scrollAnimDuration");

		if (elem->has("maxVisible"))
			mMaxVisible = (int)elem->get<float>("maxVisible");

		if (elem->has("defaultImage"))
			mDefaultImage = elem->get<std::string>("defaultImage");

		if (elem->has("textFallbackColor"))
			mTextFallbackColor = elem->get<unsigned int>("textFallbackColor");

		if (elem->has("textFallbackFont") || elem->has("textFallbackFontSize"))
		{
			std::string fontPath = (elem->has("textFallbackFont")) ? elem->get<std::string>("textFallbackFont") : "";
			// fontSize from theme is a RESOLUTION_FLOAT (normalized by theme resolution),
			// so multiply by screen height to get actual pixel size (same as Font::getFromTheme)
			float sh = (float)Renderer::getScreenHeight();
			int fontSize = (elem->has("textFallbackFontSize"))
				? (int)(sh * elem->get<float>("textFallbackFontSize"))
				: FONT_SIZE_MEDIUM;
			if (fontSize <= 0)
				fontSize = FONT_SIZE_MEDIUM;
			if (!fontPath.empty())
				mTextFallbackFont = Font::get(fontSize, fontPath);
			else
				mTextFallbackFont = Font::get(fontSize);
		}

		if (elem->has("scrollSound"))
			mScrollSound = elem->get<std::string>("scrollSound");
	}
}

#endif // ES_APP_COMPONENTS_MARQUEE_LIST_COMPONENT_H
