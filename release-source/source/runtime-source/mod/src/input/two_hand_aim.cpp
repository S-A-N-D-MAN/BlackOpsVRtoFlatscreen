// SPDX-License-Identifier: GPL-3.0-only
#include "input/two_hand_aim.hpp"
#include <bit>
#include <cmath>
namespace bovr::input {
namespace {
namespace xr=wawvr::xr;
bool finite(xr::Vec3f v){return std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z);}
float dot(xr::Vec3f a,xr::Vec3f b){return a.x*b.x+a.y*b.y+a.z*b.z;}
xr::Vec3f cross(xr::Vec3f a,xr::Vec3f b){return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};}
bool unit(xr::Quaternionf q){const float n=q.x*q.x+q.y*q.y+q.z*q.z+q.w*q.w;return std::isfinite(n)&&std::fabs(n-1)<.002F;}
bool normalize(xr::Vec3f* v){const float n=dot(*v,*v);if(!finite(*v)||!std::isfinite(n)||n<1e-10F)return false;
 const float s=1/std::sqrt(n);*v={v->x*s,v->y*s,v->z*s};return finite(*v);}
bool rigid(const xr::Basis3f& b){return finite(b.forward)&&finite(b.left)&&finite(b.up)&&
 std::fabs(dot(b.forward,b.forward)-1)<.002F&&std::fabs(dot(b.left,b.left)-1)<.002F&&
 std::fabs(dot(b.up,b.up)-1)<.002F&&std::fabs(dot(b.forward,b.left))<.002F&&
 std::fabs(dot(b.forward,b.up))<.002F&&std::fabs(dot(b.left,b.up))<.002F&&dot(cross(b.forward,b.left),b.up)>.998F;}
bool same(float a,float b){return std::bit_cast<unsigned>(a)==std::bit_cast<unsigned>(b);}
bool same(xr::Vec3f a,xr::Vec3f b){return same(a.x,b.x)&&same(a.y,b.y)&&same(a.z,b.z);}
bool same(xr::Quaternionf a,xr::Quaternionf b){return same(a.x,b.x)&&same(a.y,b.y)&&same(a.z,b.z)&&same(a.w,b.w);}
bool same(const xr::TrackedPose& a,const xr::TrackedPose& b){return a.active==b.active&&a.orientation_valid==b.orientation_valid&&
 a.position_valid==b.position_valid&&a.orientation_tracked==b.orientation_tracked&&a.position_tracked==b.position_tracked&&
 same(a.pose.orientation,b.pose.orientation)&&same(a.pose.position,b.pose.position);}
}
bool same_support_actions(const xr::ActionSnapshot& a,const xr::ActionSnapshot& b) noexcept {
 for(unsigned i=0;i<xr::kHandCount;++i){const auto& x=a.hands[i];const auto& y=b.hands[i];
  if(!same(x.grip,y.grip)||x.squeeze.active!=y.squeeze.active||x.squeeze.changed!=y.squeeze.changed||!same(x.squeeze.current,y.squeeze.current))return false;}
 return true;
}
SupportAimBasis two_hand_aim_basis(const xr::ActionSnapshot& actions,const xr::Posef& anchor,xr::Hand hand,bool enabled) noexcept {
 SupportAimBasis out{};using R=SupportAimReason;if(!enabled)return out;
 if(hand!=xr::Hand::Right){out.reason=R::hand;return out;}
 if(!actions.focused){out.reason=R::focus;return out;}
 // The dominant native arm stays attached to the gun. Only the support hand
 // needs squeeze to grip the front, as in COD4's default hold mode.
 const auto& squeeze=actions.hands[0].squeeze;
 if(!squeeze.active||!std::isfinite(squeeze.current)||squeeze.current<.65F||squeeze.current>1){out.reason=R::squeeze;return out;}
 const auto& right=actions.hands[1];const auto& left=actions.hands[0];
 for(const auto& h:actions.hands)if(!h.grip.active||!h.grip.position_valid||!h.grip.position_tracked||!finite(h.grip.pose.position)){out.reason=R::tracking;return out;}
 if(!right.aim.active||!right.aim.orientation_valid||!right.aim.orientation_tracked||!unit(right.aim.pose.orientation)){out.reason=R::tracking;return out;}
 if(!finite(anchor.position)||!unit(anchor.orientation)){out.reason=R::anchor;return out;}
 // Both endpoints are transformed in one original anchor domain. Quaternion
 // grip/left-aim rotations are deliberately unused by the geometric contract.
 const xr::Posef rear{right.aim.pose.orientation,anchor.position};
 const auto r=xr::OpenXrPoseToIwRelative(rear,anchor,1.F);
 const auto& lp=left.grip.pose.position;const auto& rp=right.grip.pose.position;
 // Subtract before applying the shared anchor rotation. Common translation
 // cancels without introducing large anchor-relative endpoint cancellation.
 xr::Vec3f forward=xr::OpenXrVectorToIw(xr::Rotate(xr::Conjugate(xr::Normalize(anchor.orientation)),{lp.x-rp.x,lp.y-rp.y,lp.z-rp.z}));
 const float n=dot(forward,forward);
 constexpr float minimum=.1016F,maximum=.8128F;
 if(!std::isfinite(n)||n<minimum*minimum||n>maximum*maximum){out.reason=R::separation;return out;}
 out.separation_meters=std::sqrt(n);
 if(!rigid(r.axis)||dot(forward,r.axis.forward)<.0254F){out.reason=R::behind;return out;}
 if(!normalize(&forward)){out.reason=R::basis;return out;}
 if(std::hypot(forward.x,forward.y)<.001F){out.reason=R::pole;return out;}
 const float along=dot(r.axis.up,forward);
 xr::Vec3f up{r.axis.up.x-forward.x*along,r.axis.up.y-forward.y*along,r.axis.up.z-forward.z*along};
 if(!normalize(&up)){up=cross(r.axis.left,forward);if(!normalize(&up)){out.reason=R::basis;return out;}}
 auto left_axis=cross(up,forward);if(!normalize(&left_axis)){out.reason=R::basis;return out;}
 up=cross(forward,left_axis);if(!normalize(&up)){out.reason=R::basis;return out;}
 out.local={forward,left_axis,up};
 if(!rigid(out.local)){out.local={};out.reason=R::basis;return out;}
 out.reason=R::pair;out.paired=true;return out;
}
}
