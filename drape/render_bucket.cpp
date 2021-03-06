#include "drape/render_bucket.hpp"

#include "drape/overlay_handle.hpp"
#include "drape/attribute_buffer_mutator.hpp"
#include "drape/vertex_array_buffer.hpp"
#include "drape/overlay_tree.hpp"

#include "base/stl_add.hpp"
#include "std/bind.hpp"

namespace dp
{

RenderBucket::RenderBucket(TransferPointer<VertexArrayBuffer> buffer)
  : m_buffer(buffer)
{
}

RenderBucket::~RenderBucket()
{
  m_buffer.Destroy();
  (void)GetRangeDeletor(m_overlay, MasterPointerDeleter())();
}

RefPointer<VertexArrayBuffer> RenderBucket::GetBuffer()
{
  return m_buffer.GetRefPointer();
}

void RenderBucket::AddOverlayHandle(TransferPointer<OverlayHandle> handle)
{
  m_overlay.push_back(MasterPointer<OverlayHandle>(handle));
}

void RenderBucket::Update(ScreenBase const & modelView)
{
  for_each(m_overlay.begin(), m_overlay.end(), bind(&OverlayHandle::Update,
                                                    bind(&dp::NonConstGetter<OverlayHandle>, _1),
                                                    modelView));
}

void RenderBucket::CollectOverlayHandles(RefPointer<OverlayTree> tree)
{
  for_each(m_overlay.begin(), m_overlay.end(), bind(&OverlayTree::Add, tree.GetRaw(),
                                                    bind(&MasterPointer<OverlayHandle>::GetRefPointer, _1)));
}

void RenderBucket::Render(ScreenBase const & screen)
{
  if (!m_overlay.empty())
  {
    // in simple case when overlay is symbol each element will be contains 6 indexes
    AttributeBufferMutator attributeMutator;
    IndexBufferMutator indexMutator(6 * m_overlay.size());
    RefPointer<IndexBufferMutator> rfpIndex = MakeStackRefPointer(&indexMutator);
    RefPointer<AttributeBufferMutator> rfpAttrib = MakeStackRefPointer(&attributeMutator);

    for_each(m_overlay.begin(), m_overlay.end(), [&] (MasterPointer<OverlayHandle> handle)
    {
      if (handle->IsValid() && handle->IsVisible())
      {
        handle->GetElementIndexes(rfpIndex);
        if (handle->HasDynamicAttributes())
          handle->GetAttributeMutation(rfpAttrib, screen);
      }
    });

    m_buffer->ApplyMutation(rfpIndex, rfpAttrib);
  }
  m_buffer->Render();
}

} // namespace dp
